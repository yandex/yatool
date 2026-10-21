import copy
import logging
from pathlib import Path
from datetime import datetime

from devtools.ya.core import yarg
from devtools.ya.core.common_opts import CrossCompilationOptions
from devtools.ya.build import build_opts, ya_make

from devtools.ya.ide.gradle.common import YaIdeGradleException, ExclusiveLock
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.graph import _JavaSemGraph


class _Builder:
    """Build required targets"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph

    def build(self) -> str:
        """Extract build targets from sem-graph and build they"""
        try:
            java_rel_targets: list[Path] = list(self.sem_graph.get_run_java_program_rel_targets())
            proto_rel_targets: list[Path] = []
            rel_targets = self.sem_graph.get_rel_targets()
            for rel_target, consumer_type, module_type in rel_targets:
                if self.config.is_exclude_target(rel_target):
                    # Always build exclude targets
                    pass
                elif (consumer_type not in ["", _JavaSemGraph.LIBRARY, _JavaSemGraph.CONTRIB]) or (
                    consumer_type == _JavaSemGraph.CONTRIB and self.config.params.collect_contribs
                ):
                    # Fast way - always build specials and contribs, if enabled
                    pass
                elif self.config.in_rel_targets(rel_target):
                    if consumer_type in ["", _JavaSemGraph.LIBRARY]:
                        # Skip libraries in exporting targets
                        continue
                    # Always build contribs in exporting targets
                if module_type == _JavaSemGraph.JAR_PROTO_SEM:
                    # Collect all proto for build to another list
                    proto_rel_targets.append(rel_target)
                else:
                    # Collect other targets
                    java_rel_targets.append(rel_target)
        except Exception as e:
            return f'Fail extract build targets from sem-graph {self.sem_graph.sem_graph_file}: {e}'

        self._build_rel_targets(
            java_rel_targets,
            proto_rel_targets,
            self.sem_graph.foreign_targets if self.config.params.build_foreign else [],
        )

    def _build_rel_targets(
        self, java_rel_targets: list[Path], proto_rel_targets: list[Path] = None, foreign_rel_targets: list[Path] = None
    ) -> None:
        """Build all relative targets in one request."""

        if not java_rel_targets and not proto_rel_targets and not foreign_rel_targets:
            return  # Nothing to build

        import app_ctx

        try:
            ya_make_opts = yarg.merge_opts(build_opts.ya_make_options(build_type='release', free_build_targets=True))
            params = yarg.merge_params(ya_make_opts.initialize(self.config.provided_make_args))
            java_opts = yarg.merge_opts(build_opts.ya_make_options(build_type='release', free_build_targets=True))
            java_flags = yarg.merge_params(java_opts.initialize(self.config.params.ya_make_extra)).flags
            params.bld_dir = self.config.params.bld_dir
            params.arc_root = str(self.config.arcadia_root)
            params.bld_root = self.config.params.bld_root
            params.ignore_recurses = True
            if self.config.output_root != self.config.arcadia_root:
                # Exported project awaits build results in output root, not in arcadia.
                # Symlinks are refreshed on every build, unlike existing hardlinks.
                params.symlink_root = str(self.config.output_root)
                params.create_symlinks = True
            params.build_graph_cache_force_local_cl = (
                True  # workaround for local changelist (enabled by default, but not work here)
            )
            params.ymake_internal_servermode = (
                False  # workaround for Error[ToolConf]: in ...: Tool is not found in a host graph
            )
            params.rel_targets = (
                []  # workaround for Error: Failed in build process: 'Params' object has no attribute 'rel_targets'
            )
            # workaround get few changelists for ymakes at the same time, else some ymakes without changelist
            params.build_graph_arc_command_timeout = 5
            params.build_graph_arc_commit_lock_timeout = 5

            base_platforms = params.target_platforms or [
                CrossCompilationOptions.make_platform(params.host_platform or 'default')
            ]
            params.target_platforms = []

            def add_platforms(rel_targets: list[str], flags: dict[str, str], *, java=False):
                for base_platform in base_platforms:
                    platform = copy.deepcopy(base_platform)
                    platform['flags'] = dict(flags, **platform.get('flags', {}))
                    if java:
                        platform['flags'].update(BUILD_LANGUAGES='JAVA', SOURCES_JAR='yes')
                    platform['ignore_recurses'] = True
                    platform['targets'] = rel_targets
                    params.target_platforms.append(platform)

            build_infos = []
            if foreign_rel_targets:
                foreign_rel_targets = sorted({str(target) for target in foreign_rel_targets})
                add_platforms(foreign_rel_targets, params.flags)
                build_infos.append(f"{len(foreign_rel_targets)} foreign targets")

            if java_rel_targets or proto_rel_targets:
                java_rel_targets = sorted({str(target) for target in java_rel_targets + (proto_rel_targets or [])})
                add_platforms(java_rel_targets, java_flags, java=True)
                proto_info = f" (including {len(proto_rel_targets)} proto)" if proto_rel_targets else ""
                build_infos.append(f"{len(java_rel_targets)}{proto_info} java targets")

            build_infos.reverse()
            self.logger.info("Building %s...", " and ".join(build_infos))
            lock = None
            if self.config.params.exclusive_lock_build:
                start = datetime.now()
                self.logger.info("Getting lock for build...")
                lock = ExclusiveLock(self.config.export_root.parent / 'build')
                lock.acquire()
            try:
                if lock:
                    self.logger.info("Got lock for build [ %.4fs ]", (datetime.now() - start).total_seconds())
                builder = ya_make.YaMake(params, app_ctx)
                return_code = builder.go()
            finally:
                if lock:
                    lock.release()
            if return_code != 0:
                raise YaIdeGradleException('Some builds failed')
        except Exception as e:
            raise YaIdeGradleException(f'Failed in build process: {e}') from e
