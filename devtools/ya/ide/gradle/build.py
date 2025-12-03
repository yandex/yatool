import logging
from pathlib import Path

from devtools.ya.core import yarg
from devtools.ya.core.common_opts import CrossCompilationOptions
from devtools.ya.build import build_opts, ya_make

from devtools.ya.ide.gradle.common import YaIdeGradleException
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.graph import _JavaSemGraph


class _Builder:
    """Build required targets"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph

    def build(self) -> None:
        """Extract build targets from sem-graph and build they"""
        try:
            java_rel_targets: list[Path] = list(set(self.sem_graph.get_run_java_program_rel_targets()))
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
            raise YaIdeGradleException(
                f'Fail extract build targets from sem-graph {self.sem_graph.sem_graph_file}: {e}'
            ) from e

        self._build_rel_targets(
            java_rel_targets,
            proto_rel_targets,
            self.sem_graph.foreign_targets if self.config.params.build_foreign else [],
        )

    def _build_rel_targets(
        self, java_rel_targets: list[Path], proto_rel_targets: list[Path] = None, foreign_rel_targets: list[Path] = None
    ) -> None:
        """Build all relative targets as one requests"""

        if not java_rel_targets and not proto_rel_targets and not foreign_rel_targets:
            return  # Nothing to build

        import app_ctx

        try:
            ya_make_opts = yarg.merge_opts(build_opts.ya_make_options(build_type='release', free_build_targets=True))
            params = yarg.merge_params(ya_make_opts.initialize([]))
            params.bld_dir = self.config.params.bld_dir
            params.arc_root = str(self.config.arcadia_root)
            params.bld_root = self.config.params.bld_root
            params.ignore_recurses = True
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

            build_infos = []

            if proto_rel_targets:
                proto_rel_targets = list(set([str(proto_rel_target) for proto_rel_target in proto_rel_targets]))
                java_rel_targets += proto_rel_targets

            if java_rel_targets:
                java_rel_targets = list(set([str(java_rel_target) for java_rel_target in java_rel_targets]))
                java_rel_targets.sort()
                CrossCompilationOptions.PlatformSetAppendHook(
                    'target_platforms',
                    values=CrossCompilationOptions.generate_target_platforms_cxx,
                    transform=CrossCompilationOptions.make_platform,
                )(params, 'default')
                CrossCompilationOptions.PlatformsSetExtraConstParamHook('build_type', 'release')(params)
                CrossCompilationOptions.PlatformsSetExtraConstParamHook('ignore_recurses', True)(params)
                for flag in self.config.params.ya_make_extra + ['-DSOURCES_JAR=yes']:
                    CrossCompilationOptions.PlatformsSetExtraDictParamHook('target_platform_flag', 'flags', 'yes')(
                        params, flag[2:]
                    )
                for java_rel_target in java_rel_targets:
                    CrossCompilationOptions.PlatformsSetExtraAppendParamHook('target_platform_target', 'targets')(
                        params, java_rel_target
                    )
                build_infos.append(
                    f"{len(java_rel_targets)} {'(include ' + str(len(proto_rel_targets)) + ' proto)' if proto_rel_targets else ''} java targets"
                )

            if foreign_rel_targets:
                foreign_rel_targets = list(set([str(foreign_rel_target) for foreign_rel_target in foreign_rel_targets]))
                foreign_rel_targets.sort()
                CrossCompilationOptions.PlatformSetAppendHook(
                    'target_platforms',
                    values=CrossCompilationOptions.generate_target_platforms_cxx,
                    transform=CrossCompilationOptions.make_platform,
                )(params, 'default')
                CrossCompilationOptions.PlatformsSetExtraConstParamHook('build_type', 'release')(params)
                CrossCompilationOptions.PlatformsSetExtraConstParamHook('ignore_recurses', True)(params)
                for foreign_rel_target in foreign_rel_targets:
                    CrossCompilationOptions.PlatformsSetExtraAppendParamHook('target_platform_target', 'targets')(
                        params, foreign_rel_target
                    )
                build_infos.append(f"{len(foreign_rel_targets)} foreign targets")

            builder = ya_make.YaMake(params, app_ctx)
            self.logger.info("Building %s...", " and ".join(build_infos))
            return_code = builder.go()
            if return_code != 0:
                raise YaIdeGradleException('Some builds failed')
        except Exception as e:
            raise YaIdeGradleException(f'Failed in build process: {e}') from e
