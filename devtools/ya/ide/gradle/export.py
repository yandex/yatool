import os
import logging
import subprocess
import time
from pathlib import Path

from devtools.ya.yalibrary import sjson

from devtools.ya.ide.gradle.common import tracer, YaIdeGradleException
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.graph import _JavaSemGraph


class _Exporter:
    """Generating files to export root"""

    _GRADLE_DAEMON_JVMARGS = 'org.gradle.jvmargs'
    _KOTLIN_DAEMON_JVMARGS = 'kotlin.daemon.jvmargs'

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph
        self.project_name: str = None
        self.attrs_for_all_templates: list[str] = [
            "symlinks_to_generated = " + ("false" if self.config.params.disable_generated_symlinks else "true")
        ]
        self.attrs_for_all_templates += self.config.params.yexport_toml
        if self.config.output_root != self.config.arcadia_root:
            self.attrs_for_all_templates += [f"output_root = '{self.config.output_root}'"]

    def export(self) -> None:
        """Generate files from sem-graph by yexport"""
        self._make_project_name()
        self._make_project_gradle_props()
        self._apply_force_jdk_version()
        self._fill_common_dir()
        self._make_yexport_toml()
        with tracer.scope('export>yexport'):
            self._run_yexport()

    def _make_project_name(self) -> None:
        """Fill project name by options and targets"""
        self.project_name = (
            self.config.params.gradle_name
            if self.config.params.gradle_name
            else Path(self.config.params.abs_targets[0]).name
        )
        self.logger.info("Project name: %s", self.project_name)

    def _make_project_gradle_props(self) -> None:
        """Make project specific gradle.properties file"""
        project_gradle_properties = []
        for prop, value in self.config.get_project_gradle_props().items():
            project_gradle_properties = self._apply_gradle_property(project_gradle_properties, prop, value)
        if self.config.params.gradle_daemon_jvmargs:
            project_gradle_properties = self._apply_gradle_property(
                project_gradle_properties, self._GRADLE_DAEMON_JVMARGS, self.config.params.gradle_daemon_jvmargs
            )
        if self.config.params.kotlin_daemon_jvmargs:
            project_gradle_properties = self._apply_gradle_property(
                project_gradle_properties, self._KOTLIN_DAEMON_JVMARGS, self.config.params.kotlin_daemon_jvmargs
            )
        gradle_jdk_path = self.sem_graph.get_jdk_path(self.sem_graph.gradle_jdk_version)
        if gradle_jdk_path != self.sem_graph.JDK_PATH_NOT_FOUND:
            self.attrs_for_all_templates += [
                f"gradle_jdk_version = '{self.sem_graph.gradle_jdk_version}'",
                f"gradle_jdk_path = '{gradle_jdk_path}'",
            ]
            project_gradle_properties.append(f"org.gradle.java.home={gradle_jdk_path}")

        if self.sem_graph.jdk_paths:
            project_gradle_properties.append(
                f"org.gradle.java.installations.fromEnv={','.join('JDK' + str(jdk_version) for jdk_version in self.sem_graph.jdk_paths.keys())}"
            )
            project_gradle_properties.append(
                f"org.gradle.java.installations.paths={','.join(jdk_path for jdk_path in self.sem_graph.jdk_paths.values())}"
            )

        project_gradle_properties_file = self.config.export_root / _JavaSemConfig.GRADLE_PROPS
        with project_gradle_properties_file.open('w') as f:
            f.write('\n'.join(project_gradle_properties))

    @staticmethod
    def _apply_gradle_property(gradle_properties: list[str], property: str, value: str) -> list[str]:
        value = value.strip()
        property_line = property + '=' + value
        property_applied = False
        patched_gradle_properties: list[str] = []
        for gradle_property in gradle_properties:
            if gradle_property.startswith(property + '='):
                patched_gradle_properties.append(property_line)
                property_applied = True
            else:
                patched_gradle_properties.append(gradle_property)
        if not property_applied:
            patched_gradle_properties.append(property_line)
        return patched_gradle_properties

    def _apply_force_jdk_version(self) -> None:
        """Apply force JDK version from options, if exists"""
        if not self.config.params.force_jdk_version:
            return
        force_jdk_path = self.sem_graph.get_jdk_path(int(self.config.params.force_jdk_version))
        if force_jdk_path != self.sem_graph.JDK_PATH_NOT_FOUND:
            self.attrs_for_all_templates += [
                f"force_jdk_version = '{self.config.params.force_jdk_version}'",
                f"force_jdk_path = '{force_jdk_path}'",
            ]

    def _fill_common_dir(self) -> None:
        common_dir = os.path.commonpath(self.config.params.rel_targets)
        if not common_dir:
            return
        common_path = self.config.arcadia_root / common_dir
        if common_path.is_relative_to(self.config.settings_root):
            self.attrs_for_all_templates += [
                f"common_dir = '{self.config.settings_root.relative_to(self.config.arcadia_root)}'",
            ]

    def _make_yexport_toml(self) -> None:
        """Make yexport.toml with yexport special options"""
        self.logger.info("Path prefixes for skip in yexport: %s", self.config.params.rel_targets)
        yexport_toml = self.config.ymake_root / 'yexport.toml'
        with yexport_toml.open('w') as f:
            f.write(
                '\n'.join(
                    [
                        '[add_attrs.root]',
                        *self.attrs_for_all_templates,
                        '',
                        '[add_attrs.dir]',
                        *self.attrs_for_all_templates,
                        f'build_contribs = {'true' if self.config.params.collect_contribs else 'false'}',
                        f'disable_errorprone = {'true' if self.config.params.disable_errorprone else 'false'}',
                        f'disable_lombok_plugin = {'true' if self.config.params.disable_lombok_plugin else 'false'}',
                        '',
                        '[add_attrs.target]',
                        *self.attrs_for_all_templates,
                        '',
                        '[[target_replacements]]',
                        f'skip_path_prefixes = [ "{'", "'.join(self.config.params.rel_targets)}" ]',
                        '',
                        '[[target_replacements.addition]]',
                        f'name = "{_JavaSemGraph._CONSUMER_PREBUILT_SEM}"',
                        'args = []',
                        '[[target_replacements.addition]]',
                        f'name = "{_JavaSemGraph._IGNORED_SEM}"',
                        'args = []',
                    ]
                )
            )

    def _run_yexport(self) -> None:
        """Generating export files by run yexport"""
        yexport_cmd = [
            self.config.yexport_bin,
            '--arcadia-root',
            str(self.config.arcadia_root),
            '--export-root',
            str(self.config.export_root),
            '--project-root',
            str(self.config.settings_root),
            '--configuration',
            str(self.config.ymake_root),
            '--semantic-graph',
            str(self.sem_graph.sem_graph_file),
        ]
        if self.config.params.yexport_debug_mode is not None:
            yexport_cmd += ["--debug-mode", str(self.config.params.yexport_debug_mode)]
        yexport_cmd += ['--fail-on-error']
        yexport_cmd += ['--generator', 'ide-gradle']
        if self.project_name is not None:
            yexport_cmd += ['--target', self.project_name]

        self.logger.info("Generate by yexport command:\n%s", ' '.join(yexport_cmd))
        t = time.time()
        r = subprocess.run(yexport_cmd, capture_output=True, text=True)
        if r.returncode != 0:
            self.logger.error("Fail generating by yexport:\n%s", r.stderr)
            raise YaIdeGradleException(
                '\n'.join(
                    [
                        f'Fail generating by yexport with exit_code={r.returncode}',
                        'Please, create ticket [to support queue](https://st.yandex-team.ru/createTicket?queue=DEVTOOLSSUPPORT&_form=6668786540e3616bc95905d3)',
                    ]
                )
            )
        for line in r.stderr.split("\n"):
            try:
                event = sjson.loads(line.strip())
                if ("_typename" not in event) or (event["_typename"] != "NEvent.TStageStat"):
                    continue
            except RuntimeError:
                continue
            # self.logger.info("Yexport stat: %s", event)
            stage = tracer.start('export>yexport>' + event["Stage"], start_time=t)
            t += event["SumSec"]  # emulate stage duration
            stage.finish(finish_time=t)
