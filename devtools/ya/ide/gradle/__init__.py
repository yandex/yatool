import os
import logging
import shutil
import subprocess
from typing import Iterable
from pathlib import Path

from core import config as core_config, yarg, stage_tracer
from build import build_opts, graph as build_graph, ya_make
from build.sem_graph import SemLang, SemConfig, SemNode, SemGraph
from yalibrary import platform_matcher
from exts import hashing


class YaIdeGradleException(Exception):
    pass


class _JavaSemConfig(SemConfig):
    """Check and use command line options for configure roots and flags"""

    GRADLE_PROPS_FILE: Path = Path.home() / '.gradle' / 'gradle.properties'
    GRADLE_REQUIRED_PROPS: list[str] = [
        'bucketUsername',
        'bucketPassword',
        'systemProp.gradle.wrapperUser',
        'systemProp.gradle.wrapperPassword',
    ]

    EXPORT_ROOT_BASE: Path = Path(core_config.misc_root()) / 'gradle'  # Base folder of all export roots

    def __init__(self, params):
        if platform_matcher.is_windows():
            raise YaIdeGradleException("Windows is not supported in ya ide gradle")
        super().__init__(SemLang.JAVA(), params)
        self.logger = logging.getLogger(type(self).__name__)
        self.settings_root = self._get_settings_root()
        self.appended_abs_targets = []
        self.appended_rel_targets = []

        if not self.params.remove:
            self._check_gradle_props()

    def append_rel_targets(self, rel_targets: list[Path]) -> None:
        for rel_target in rel_targets:
            self.params.rel_targets.append(rel_target)
            self.appended_rel_targets.append(rel_target)
            abs_target = str(self.arcadia_root / rel_target)
            self.params.abs_targets.append(abs_target)
            self.appended_abs_targets.append(abs_target)

    def _check_gradle_props(self) -> None:
        """Check exists all required gradle properties"""
        errors = []
        if not _JavaSemConfig.GRADLE_PROPS_FILE.is_file():
            errors.append(f'File {_JavaSemConfig.GRADLE_PROPS_FILE} does not exist')
        else:
            with _JavaSemConfig.GRADLE_PROPS_FILE.open() as f:
                props = f.read()
            for prop in _JavaSemConfig.GRADLE_REQUIRED_PROPS:
                if prop not in props:
                    errors.append(f'Required property {prop} is not defined in {_JavaSemConfig.GRADLE_PROPS_FILE} file')
        if errors:
            raise YaIdeGradleException(
                '\n'.join(
                    [
                        f'Invalid Gradle properties file {_JavaSemConfig.GRADLE_PROPS_FILE}:',
                        *errors,
                        '',
                        'Please, read more about work with Bucket https://docs.yandex-team.ru/bucket/gradle#autentifikaciya'
                        'Token can be taken from here https://oauth.yandex-team.ru/authorize?response_type=token&client_id=bf8b6a8a109242daaf62bce9d6609b3b',
                    ]
                )
            )

    def _get_export_root(self) -> Path:
        """Create export_root path by hash of targets"""
        targets_hash = hashing.fast_hash(':'.join(sorted(self.params.abs_targets)))
        export_root = _JavaSemConfig.EXPORT_ROOT_BASE / targets_hash
        self.logger.info("Export root: %s", export_root)
        return export_root

    def _get_settings_root(self) -> Path:
        """Create settings_root path by options and targets"""
        settings_root = (
            self.arcadia_root / Path(self.params.settings_root)
            if self.params.settings_root
            else Path(self.params.abs_targets[0])
        )
        self.logger.info("Settings root: %s", settings_root)
        if not settings_root.exists() or not settings_root.is_dir():
            raise YaIdeGradleException('Not found settings root directory')
        return settings_root


class _SymlinkCollector:
    """Iterate on settings and build root and call collect function for every place, where symlinks waited"""

    SETTINGS_FILES: list[str] = ["settings.gradle.kts", "gradlew", "gradlew.bat"]  # Files for symlink to settings root
    SETTINGS_MKDIRS: list[str] = [".gradle", ".idea"]  # Folders for creating at settings root
    SETTINGS_DIRS: list[str] = SETTINGS_MKDIRS + ["gradle"]  # Folders for symlink to settings root

    BUILD_SKIP_ROOT_DIRS: list[str] = SETTINGS_DIRS + [
        _JavaSemConfig.YMAKE_DIR
    ]  # Skipped for build directories in export root
    BUILD_FILE: str = "build.gradle.kts"  # Filename for create build symlinks

    def __init__(self, java_sem_config: _JavaSemConfig):
        self.config: _JavaSemConfig = java_sem_config

    def collect_symlinks(self) -> Iterable[tuple[Path]]:
        yield from self._collect_settings_symlinks()
        yield from self._collect_build_symlinks()

    def _collect_settings_symlinks(self) -> Iterable[tuple[Path]]:
        """Collect symlinks for each settings files/dirs"""
        for mkdir in _SymlinkCollector.SETTINGS_MKDIRS:
            (self.config.export_root / mkdir).mkdir(0o755, parents=True, exist_ok=True)
        for export_file in self.config.export_root.iterdir():
            basename = export_file.name
            if (basename in _SymlinkCollector.SETTINGS_FILES and export_file.is_file()) or (
                basename in _SymlinkCollector.SETTINGS_DIRS and export_file.is_dir()
            ):
                arcadia_file = self.config.settings_root / basename
                yield export_file, arcadia_file

    def _collect_build_symlinks(self) -> Iterable[tuple[Path]]:
        """Collect symlinks for each build files/dirs from arcadia to export"""
        for export_file in self.config.export_root.iterdir():
            basename = export_file.name
            if basename not in _SymlinkCollector.BUILD_SKIP_ROOT_DIRS and export_file.is_dir():
                export_dir = export_file
                for walk_root, _, files in export_dir.walk():
                    for file in files:
                        if file == _SymlinkCollector.BUILD_FILE:
                            export_file = walk_root / file
                            arcadia_file = self.config.arcadia_root / export_file.relative_to(self.config.export_root)
                            yield export_file, arcadia_file
            elif basename == _SymlinkCollector.BUILD_FILE and export_file.is_file():
                arcadia_file = self.config.arcadia_root / basename
                yield export_file, arcadia_file


class _ExistsSymlinkCollector(_SymlinkCollector):
    """Collect exists symlinks for remove later"""

    def __init__(self, java_sem_config: _JavaSemConfig):
        super().__init__(java_sem_config)
        self.logger = logging.getLogger(type(self).__name__)
        self.symlinks: dict[Path, Path] = {}

    def collect(self):
        """Collect already exists symlinks"""
        if not self.config.export_root.exists():
            return
        for export_file, arcadia_file in self.collect_symlinks():
            if arcadia_file.is_symlink() and arcadia_file.resolve() == export_file:
                self.symlinks[arcadia_file] = export_file

    def remove_symlinks(self) -> None:
        """Remove symlinks from arcadia files to export files"""
        for arcadia_file, export_file in self.symlinks.items():
            try:
                arcadia_file.unlink()
            except Exception as e:
                self.logger.warning(
                    "Can't remove symlink '%s' -> '%s': %s", arcadia_file, export_file, e, exc_info=True
                )


class _NewSymlinkCollector(_SymlinkCollector):
    """Collect new symlinks for create later, exclude already exists"""

    def __init__(self, exists_symlinks: _ExistsSymlinkCollector):
        super().__init__(exists_symlinks.config)
        self.logger = logging.getLogger(type(self).__name__)
        self.exists_symlinks: _ExistsSymlinkCollector = exists_symlinks
        self.symlinks: dict[Path, Path] = {}
        self.has_errors: bool = False

    def collect(self):
        """Collect new symlinks for creating, skip already exists symlinks"""
        for export_file, arcadia_file in self.collect_symlinks():
            if arcadia_file in self.exists_symlinks.symlinks:
                # Already exists, do nothing
                del self.exists_symlinks.symlinks[arcadia_file]
            elif not arcadia_file.exists():
                self.symlinks[arcadia_file] = export_file
            elif arcadia_file.is_symlink() and arcadia_file.resolve().is_relative_to(_JavaSemConfig.EXPORT_ROOT_BASE):
                self.logger.error("Already symlink to another project %s -> %s", arcadia_file, arcadia_file.resolve())
                self.has_errors = True

    def create_symlinks(self) -> None:
        """Create symlinks from arcadia files to export files"""
        for arcadia_file, export_file in self.symlinks.items():
            try:
                arcadia_file.symlink_to(export_file, export_file.is_dir())
            except Exception as e:
                self.logger.error("Can't create symlink '%s' -> '%s': %s", arcadia_file, export_file, e, exc_info=True)
                self.has_errors = True


class _JavaSemGraph(SemGraph):
    """Creating and reading sem-graph"""

    def __init__(self, config: _JavaSemConfig):
        super().__init__(config, skip_invalid=True)
        self.logger = logging.getLogger(type(self).__name__)

    def make(self) -> None:
        """Make sem-graph file by ymake"""
        super().make()
        run_java_program_rel_targets = self._get_run_java_program_rel_targets()
        if run_java_program_rel_targets:
            self.config.append_rel_targets(run_java_program_rel_targets)
            self.logger.info("Updated targets: %s", self.config.params.rel_targets)
            super().make()  # Remake sem-graph with appended RUN_JAVA_PROGRAM targets

    def get_rel_targets(self) -> list[(Path, bool)]:
        """Get list of rel_targets from sem-graph with is_contrib flag for each"""
        rel_targets = []
        for node in self.read():
            if not isinstance(node, SemNode):
                continue  # interest only nodes
            if not node.name.startswith('$B/') or not node.name.endswith('.jar'):  # Search only *.jar with semantics
                continue
            rel_target = Path(node.name.replace('$B/', '')).parent  # Relative target - directory of *.jar
            is_contrib = False
            for semantic in node.semantics:
                if len(semantic.sems) == 2 and semantic.sems[0] == 'consumer-type' and semantic.sems[1] == 'contrib':
                    is_contrib = True
                    break
            rel_targets.append((rel_target, is_contrib))
        return rel_targets

    def _get_run_java_program_rel_targets(self) -> list[Path]:
        """Find RUN_JAVA_PROGRAMs in sem-graph and extract additional targets for they"""
        try:
            run_java_program_rel_targets = []
            for node in self.read():
                if not isinstance(node, SemNode):
                    continue  # interest only nodes
                for semantic in node.semantics:
                    if (
                        len(semantic.sems) == 2
                        and semantic.sems[0] == "runs-classpath"
                        and semantic.sems[1].startswith('@')
                        and semantic.sems[1].endswith('.cplst')
                    ):
                        cplst = semantic.sems[1][1:]
                        if not cplst:
                            raise YaIdeGradleException(f'Empty classpath list in node {node}')
                        # target is directory of cplst
                        run_java_program_rel_targets.append(
                            os.path.relpath(Path(cplst).parent, self.config.export_root)
                        )
            return run_java_program_rel_targets
        except Exception as e:
            raise YaIdeGradleException(f'Fail extract additional RUN_JAVA_PROGRAM targets from sem-graph: {e}') from e


class _Exporter:
    """Generating files to export root"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph

    def export(self) -> None:
        """Generate files from sem-graph by yexport"""
        project_name = (
            self.config.params.gradle_name
            if self.config.params.gradle_name
            else Path(self.config.params.abs_targets[0]).name
        )
        self.logger.info("Project name: %s", project_name)

        self.logger.info("Path prefixes for skip in yexport:\n%s", self.config.params.rel_targets)

        yexport_toml = self.config.ymake_root / 'yexport.toml'
        with yexport_toml.open('w') as f:
            f.write(
                '\n'.join(
                    [
                        '[add_attrs.dir]',
                        f'build_contribs = {'true' if self.config.params.build_contribs else 'false'}',
                        '',
                        '[[target_replacements]]',
                        f'skip_path_prefixes = [ "{'", "'.join(self.config.params.rel_targets)}" ]',
                        '',
                        '[[target_replacements.addition]]',
                        'name = "consumer-prebuilt"',
                        'args = []',
                        '[[target_replacements.addition]]',
                        'name = "IGNORED"',
                        'args = []',
                    ]
                )
            )

        yexport_cmd = [
            self.config.yexport_bin,
            '--arcadia-root',
            str(self.config.arcadia_root),
            '--export-root',
            str(self.config.export_root),
            '--configuration',
            str(self.config.ymake_root),
            '--semantic-graph',
            str(self.sem_graph.sem_graph_file),
        ]
        if self.config.params.yexport_debug_mode is not None:
            yexport_cmd += ["--debug-mode", str(self.config.params.yexport_debug_mode)]
        yexport_cmd += ['--generator', 'ide-gradle', '--target', project_name]

        self.logger.info("Generate by yexport command:\n%s", ' '.join(yexport_cmd))
        r = subprocess.run(yexport_cmd, capture_output=True, text=True)
        if r.returncode != 0:
            self.logger.error("Fail generating by yexport:\n%s", r.stderr)
            raise YaIdeGradleException(f'Fail generating by yexport with exit_code={r.returncode}')


class _Builder:
    """Build required targets"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph

    def build(self) -> None:
        """Extract build targets from sem-graph and build they"""
        try:
            build_rel_targets = self.config.appended_rel_targets
            rel_targets = self.sem_graph.get_rel_targets()
            for rel_target, is_contrib in rel_targets:
                in_rel_targets = False
                for conf_rel_target in self.config.params.rel_targets:
                    if rel_target.is_relative_to(Path(conf_rel_target)):
                        in_rel_targets = True
                        break

                if in_rel_targets:
                    # Skip target, already in input targets
                    continue
                elif self.config.params.build_contribs or not is_contrib:
                    # Build all non-input or not contrib targets
                    build_rel_targets.append(rel_target)
        except Exception as e:
            raise YaIdeGradleException(
                f'Fail extract build targets from sem-graph {self.sem_graph.sem_graph_file}: {e}'
            ) from e

        if not build_rel_targets:
            return

        import app_ctx

        try:
            ya_make_opts = yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True))
            opts = yarg.merge_params(ya_make_opts.initialize(self.config.params.ya_make_extra))

            arcadia_root = self.config.arcadia_root

            opts.bld_dir = self.config.params.bld_dir
            opts.arc_root = str(arcadia_root)
            opts.bld_root = self.config.params.bld_root

            opts.rel_targets = list()
            opts.abs_targets = list()
            for build_rel_target in build_rel_targets:  # Add all targets for build simultaneously
                opts.rel_targets.append(str(build_rel_target))
                opts.abs_targets.append(str(arcadia_root / build_rel_target))

            self.logger.info("Making building graph")
            with app_ctx.event_queue.subscription_scope(ya_make.DisplayMessageSubscriber(opts, app_ctx.display)):
                graph, _, _, _, _ = build_graph.build_graph_and_tests(opts, check=True, display=app_ctx.display)
            self.logger.info("Build all by graph")
            builder = ya_make.YaMake(opts, app_ctx, graph=graph, tests=[])
            return_code = builder.go()
            if return_code != 0:
                raise YaIdeGradleException('Some builds failed')
        except Exception as e:
            raise YaIdeGradleException(f'Failed in build process: {e}') from e


class _Remover:
    """Remove all symlinks and export root"""

    def __init__(self, java_sem_config: _JavaSemConfig, exists_symlinks: _ExistsSymlinkCollector):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.exists_symlinks: _ExistsSymlinkCollector = exists_symlinks

    def remove(self) -> None:
        """Remove all exists symlinks and then remove export root"""
        if self.exists_symlinks.symlinks:
            self.logger.info("Remove %d symlinks from arcadia to export root", len(self.exists_symlinks.symlinks))
            self.exists_symlinks.remove_symlinks()
        if self.config.export_root.exists():
            try:
                self.logger.info("Remove export root %s", self.config.export_root)
                shutil.rmtree(self.config.export_root)
            except Exception as e:
                self.logger.warning("While removing %s: %s", self.config.export_root, e, exc_info=True)
        else:
            self.logger.info("Export root %s already not found", self.config.export_root)


def do_gradle(params):
    """Real handler of `ya ide gradle`"""
    do_gradle_stage = stage_tracer.get_tracer("gradle").start('do_gradle')

    try:
        config = _JavaSemConfig(params)

        exists_symlinks = _ExistsSymlinkCollector(config)
        exists_symlinks.collect()

        if config.params.remove:
            remover = _Remover(config, exists_symlinks)
            remover.remove()
            return

        sem_graph = _JavaSemGraph(config)
        sem_graph.make()

        exporter = _Exporter(config, sem_graph)
        exporter.export()

        new_symlinks = _NewSymlinkCollector(exists_symlinks)
        new_symlinks.collect()
        exists_symlinks.remove_symlinks()
        new_symlinks.create_symlinks()
        if new_symlinks.has_errors:
            raise YaIdeGradleException('Some errors during creating symlinks, read the logs for more information')

        builder = _Builder(config, sem_graph)
        builder.build()

    finally:
        do_gradle_stage.finish()
