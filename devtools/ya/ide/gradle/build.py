import logging
import shutil
from pathlib import Path

from devtools.ya.core import yarg
from devtools.ya.build import build_opts, graph as build_graph, ya_make

from devtools.ya.ide.gradle.common import tracer, YaIdeGradleException
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.graph import _JavaSemGraph
from devtools.ya.ide.gradle.symlinks import _SymlinkCollector


class _Builder:
    """Build required targets"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph

    def build(self) -> None:
        """Extract build targets from sem-graph and build they"""
        try:
            build_rel_targets: list[Path] = list(set(self.sem_graph.get_run_java_program_rel_targets()))
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
                    build_rel_targets.append(rel_target)
        except Exception as e:
            raise YaIdeGradleException(
                f'Fail extract build targets from sem-graph {self.sem_graph.sem_graph_file}: {e}'
            ) from e

        if build_rel_targets:
            with tracer.scope('build>java'):
                self._build_rel_targets(build_rel_targets, proto_rel_targets)

        if self.config.params.build_foreign and self.sem_graph.foreign_targets:
            with tracer.scope('build>foreign'):
                self._build_rel_targets(self.sem_graph.foreign_targets, build_all_langs=True)

    def _build_rel_targets(
        self, build_rel_targets: list[Path], proto_rel_targets: list[Path] = None, build_all_langs: bool = False
    ) -> None:
        """Build all relative targets by one graph, build_all_langs control only java targets or all languages targets"""
        import app_ctx

        junk_ya_make = None
        try:
            if build_all_langs:
                ya_make_opts = yarg.merge_opts(
                    build_opts.ya_make_options(free_build_targets=True, build_type='release')
                )
                opts = yarg.merge_params(ya_make_opts.initialize([]))
            else:
                ya_make_opts = yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True, build_type='debug'))
                opts = yarg.merge_params(ya_make_opts.initialize(self.config.params.ya_make_extra))
                opts.dump_sources = True
                if proto_rel_targets:
                    proto_rel_targets = list(set(proto_rel_targets))
                    opts.add_result.append(".jar")  # require make symlinks to all .jar files
                    # For build PROTO_SCHEMA to jar, require build it as PEERDIR
                    # Make one temporary ya.make with JAVA_PROGRAM and PEERDIR to all proto targets
                    junk_ya_make = self.config.arcadia_root / "junk" / "ya_ide_gradle" / "ya.make"
                    _SymlinkCollector.mkdir(junk_ya_make.parent)
                    with junk_ya_make.open('w') as f:
                        f.write(
                            "\n".join(
                                [
                                    "JAVA_PROGRAM()",
                                    "PEERDIR(",
                                    *["    " + str(proto_rel_target) for proto_rel_target in proto_rel_targets],
                                    ")",
                                    "END()",
                                    "",
                                ]
                            )
                        )
                    build_rel_targets.append(junk_ya_make.parent.relative_to(self.config.arcadia_root))

            opts.bld_dir = self.config.params.bld_dir
            opts.arc_root = str(self.config.arcadia_root)
            opts.bld_root = self.config.params.bld_root

            opts.rel_targets = []
            opts.abs_targets = []
            build_rel_targets = list(set(build_rel_targets))
            for build_rel_target in build_rel_targets:  # Add all targets for build simultaneously
                opts.rel_targets.append(str(build_rel_target))
                opts.abs_targets.append(str(self.config.arcadia_root / build_rel_target))

            self.logger.info("Making building graph for %s targets...", "foreign" if build_all_langs else "java")
            with app_ctx.event_queue.subscription_scope(ya_make.DisplayMessageSubscriber(opts, app_ctx.display)):
                graph, _, _, _, _ = build_graph.build_graph_and_tests(opts, check=True, display=app_ctx.display)
            self.logger.info("Building all %s targets by graph...", "foreign" if build_all_langs else "java")
            builder = ya_make.YaMake(opts, app_ctx, graph=graph, tests=[])
            return_code = builder.go()
            if return_code != 0:
                raise YaIdeGradleException('Some builds failed')
        except Exception as e:
            raise YaIdeGradleException(f'Failed in build process: {e}') from e
        finally:
            if junk_ya_make:
                if junk_ya_make.parent.exists():
                    shutil.rmtree(junk_ya_make.parent)
