import os
import logging
import re
from pathlib import Path

from devtools.ya.core import event_handling
from devtools.ya.build.sem_graph import SemNode, SemDep, Semantic, SemGraph
from devtools.ya.yalibrary import sjson
import exts.asyncthread as core_async
from yalibrary import platform_matcher, tools

from devtools.ya.ide.gradle.common import tracer, YaIdeGradleException, ExclusiveLock
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.symlinks import _SymlinkCollector


class _ForeignEventSubscriber(event_handling.SubscriberSpecifiedTopics):
    topics = {"NEvent.TForeignPlatformTarget"}

    _FOREIGN_PLATFORM_TARGET_TYPENAME = 'NEvent.TForeignPlatformTarget'
    _FOREIGN_IDE_DEPEND_PLATFORM = 3

    def __init__(self):
        self.foreign_targets: list[str] = []

    def _action(self, event: dict) -> None:
        if not isinstance(event, dict):
            return
        if event.get('Type') == 'Error':
            self.logger.error("%s", event)
        if (
            event.get('_typename') == self._FOREIGN_PLATFORM_TARGET_TYPENAME
            and event.get('Platform') == self._FOREIGN_IDE_DEPEND_PLATFORM
        ):
            self.foreign_targets.append(event['Dir'])


class _JavaSemGraph(SemGraph):
    """Creating and reading sem-graph"""

    JDK_PATH_SEM = 'jdk_path'
    JDK_VERSION_SEM = 'jdk_version'
    REQUIRED_JDK_SEM = 'required_jdk'

    JDK_PATH_NOT_FOUND = 'NOT_FOUND'  # Magic const for not found JDK path

    _BUILD_ROOT = '$B/'  # Build root in graph

    _OLD_AP_SEM = 'annotation_processors'
    _NEW_AP_SEM = 'use_annotation_processor'
    _KAPT_SEM = 'kapt-classpaths'
    JAR_SEM = 'jar'
    JAR_PROTO_SEM = 'jar_proto'
    _SOURCE_SET_DIR_SEM = 'source_sets-dir'
    _RESOURCE_SET_DIR_SEM = 'resource_sets-dir'
    _CONSUMER_TYPE_SEM = 'consumer-type'
    _CONSUMER_PREBUILT_SEM = 'consumer-prebuilt'
    _IGNORED_SEM = 'IGNORED'

    _GENERATED = "generated"
    _SRC = "src"
    _SRC_MAIN = "src/main"
    _SRC_TEST = "src/test"

    CONTRIB = 'contrib'
    LIBRARY = 'library'

    def __init__(self, config: _JavaSemConfig):
        super().__init__(config, skip_invalid=True)
        self.logger = logging.getLogger(type(self).__name__)
        self._graph_data: list[SemNode | SemDep] = []
        self._graph_patched = False
        self.ap_class2path: dict[str, list[str]] = {}
        self.used_ap_class2path: dict[str, list[str]] = {}
        self.used_kapt_classpath2path: dict[str, list[str]] = {}
        self.use_ap_node_ids: list[int] = []
        self.node2dep_ids: dict[int, list[int]] = {}
        self.handmade_ap_re = None
        self.handmade_ap2dep_ids: dict[int, list[int]] = {}
        self.dep_paths: dict[int, Path] = {}
        self._cached_jdk_paths = {}
        self.jdk_paths: dict[int, str] = {}
        self.foreign_targets: list[str] = []
        self.gradle_jdk_version: int = 21  # by default use JDK 21 for Gradle
        if self.config.params.gradle_jdk_version:
            self.gradle_jdk_version = int(self.config.params.gradle_jdk_version)
        self.dont_symlink_jdk: bool = False  # Don't create symlinks to JDK (for tests)
        self.str_export_root = str(self.config.export_root)
        self._wait_raw_graph = None

    def make(self, **kwargs) -> None:
        """Make sem-graph file by ymake"""

        with tracer.scope('sem-graph>make'):
            self.dont_symlink_jdk = kwargs.pop('dont_symlink_jdk', False)

            # Register subscriber for foreign events
            if 'dont_foreign' not in kwargs:
                import app_ctx

                foreign_subscriber = _ForeignEventSubscriber()
                app_ctx.event_queue.subscribe(foreign_subscriber)

            # FIXME(dimdim11) - all semgraph calls save cache to one point, without exclusive lock make semgraph may fail
            with ExclusiveLock(self.config.export_root.parent / 'semgraph'):
                super().make(
                    **kwargs,
                    dump_raw_graph=(
                        self.config.ymake_root / "raw_graph"
                        if (self.config.params.yexport_debug_mode or self.config.params.dump_ymake_stderr)
                        else None
                    ),
                    foreign_on_nosem=True,
                    debug_options=self.config.params.debug_options,
                    dump_file=self.config.params.dump_file_path,
                    warn_mode=self.config.params.warn_mode,
                    dump_ymake_stderr=self.config.params.dump_ymake_stderr,
                )
            if 'dont_foreign' not in kwargs:
                if foreign_subscriber.foreign_targets:
                    self.foreign_targets = list(set(foreign_subscriber.foreign_targets))
                    self.logger.info("Foreign targets: %s", self.foreign_targets)

        if 'dont_patch_graph' not in kwargs:
            with tracer.scope('sem-graph>patch'):
                self._patch_graph()

    def get_rel_targets(self) -> list[(Path, str, str)]:
        """Get list of rel_targets from sem-graph with is_contrib flag for each"""
        rel_targets = []
        for node in self.read():
            if (
                not isinstance(node, SemNode)
                or not node.has_semantics()
                or not node.name.startswith(self._BUILD_ROOT)
                or not node.name.endswith('.jar')
            ):
                # Search only *.jar nodes with semantics
                continue
            rel_target = Path(node.name.replace(self._BUILD_ROOT, '')).parent  # Relative target - directory of *.jar
            consumer_type = ""
            for semantic in node.semantics:
                if (len(semantic.sems) == 2) and (semantic.sems[0] == self._CONSUMER_TYPE_SEM):
                    consumer_type = semantic.sems[1]
                    break
            module_type = (
                node.semantics[0].sems[0] if node.semantics[0].sems[0] in [self.JAR_SEM, self.JAR_PROTO_SEM] else ""
            )
            rel_targets.append((rel_target, consumer_type, module_type))
        return rel_targets

    def get_run_java_program_rel_targets(self) -> list[Path]:
        """Search RUN_JAVA_PROGRAMs in sem-graph and return relative targets for build"""
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
                        cplst = semantic.sems[1][len('@') : -len('.cplst')]
                        if not cplst:  # Ignore java runners without classpath
                            continue
                        # target is directory of cplst
                        run_java_program_rel_targets.append(
                            os.path.relpath(Path(cplst).parent, self.config.export_root)
                        )
            return run_java_program_rel_targets
        except Exception as e:
            raise YaIdeGradleException(f'Fail extract additional RUN_JAVA_PROGRAM targets from sem-graph: {e}') from e

    def _patch_graph(self) -> None:
        """Read sem-graph to list and patch it, if need"""
        self._get_graph_data()
        self._patch_annotation_processors()
        self._patch_jdk()
        self._patch_exclude_targets()
        if self._graph_patched:
            self._update_graph()

    def _get_graph_data(self) -> None:
        """Read sem-graph to list"""
        self._graph_data: list[SemNode | SemDep] = [item for item in self.read(all_nodes=True)]
        if (
            (self.config.params.yexport_debug_mode or self.config.params.dump_ymake_stderr)
            and self._graph_data
            and self.sem_graph_file
        ):
            # Save graph before patch for debug purposes
            self._wait_raw_graph = core_async.future(
                lambda: self._update_graph(self.sem_graph_file.parent / "raw.sem.json"), daemon=False
            )

    def _before_any_patch(self) -> None:
        if self._wait_raw_graph:
            self._wait_raw_graph()
            self._wait_raw_graph = None

    def _update_graph(self, sem_graph_file: Path = None) -> None:
        """Write patched sem-graph back to file"""
        data: list[dict] = []
        for item in self._graph_data:
            data.append(item.as_dict())
        self.update(data, sem_graph_file)

    def _patch_annotation_processors(self) -> None:
        """Patch AP semantics in graph"""
        self._configure_patch_annotation_processors()
        self._find_annotation_processors()
        self._do_patch_annotation_processors()
        if self.used_ap_class2path:  # Some AP patched
            self.logger.info(
                "Annotation processors patched in graph:\n%s",
                '\n'.join([f'{k} --> {v[0] if len(v) == 1 else v}' for k, v in self.used_ap_class2path.items()]),
            )
            self._graph_patched = True
        if self.used_kapt_classpath2path:  # Some KAPT patched
            self.logger.info(
                "KAPT patched in graph:\n%s",
                '\n'.join([f'{k} --> {v[0] if len(v) == 1 else v}' for k, v in self.used_kapt_classpath2path.items()]),
            )
            self._graph_patched = True

    def _configure_patch_annotation_processors(self) -> None:
        """Read mapping AP class -> path from configure"""
        annotation_processors_file = self.config.get_configs_dir() / "annotation_processors.json"
        if not annotation_processors_file.exists():
            raise YaIdeGradleException(f"Not found {annotation_processors_file}")
        with annotation_processors_file.open('rb') as f:
            self.ap_class2path = sjson.load(f)
        ap_paths = list(set(self.ap_class2path.values()))
        handmade_ap_paths = []
        for ap_path in ap_paths:
            if not ap_path.startswith('contrib/'):
                handmade_ap_paths.append(ap_path)
        if handmade_ap_paths:
            self.handmade_ap_re = re.compile('^(\\$B/)?(' + '/|'.join(handmade_ap_paths) + '/)')

    def _find_annotation_processors(self) -> None:
        """Find nodes with AP semantics (old or new), collect dep ids for old AP semantics"""
        for item in self._graph_data:
            if isinstance(item, SemDep):
                if item.from_id in self.node2dep_ids:  # collect dep ids for patching AP
                    self.node2dep_ids[item.from_id].append(item.to_id)
                if item.from_id in self.handmade_ap2dep_ids:  # collect dep ids for handmade APs
                    self.handmade_ap2dep_ids[item.from_id].append(item.to_id)
            if not isinstance(item, SemNode) or not item.has_semantics():
                continue
            node = item
            if self.handmade_ap_re and self.handmade_ap_re.search(node.name):
                self.handmade_ap2dep_ids[node.id] = []
            for semantic in node.semantics:
                sem0 = semantic.sems[0]
                if sem0 == self._OLD_AP_SEM or sem0 == self._KAPT_SEM:
                    self.node2dep_ids[node.id] = []  # require collect deps for patch AP classes to AP paths
                elif sem0 == self._NEW_AP_SEM:
                    self.use_ap_node_ids.append(node.id)  # collect node ids to check for versions of all AP

    def _do_patch_annotation_processors(self) -> None:
        """Patch AP semantics in graph and check all AP with versions"""
        if not self.use_ap_node_ids and not self.node2dep_ids:
            return

        self._fill_dep_paths()
        self._before_any_patch()

        # patch AP paths by deps paths and check AP has version
        for node in self._graph_data:
            if not isinstance(node, SemNode):
                continue
            # Interest only nodes with old or new semantics with AP
            if node.id not in self.node2dep_ids and node.id not in self.use_ap_node_ids:
                continue
            if node.id in self.node2dep_ids:  # require patch old to new semantic
                self._patch_node_annotation_processors(node)
            self._check_annotation_processors_has_version(node)

    def _fill_dep_paths(self) -> None:
        """Collect all used with AP deps as id -> path"""
        if not self.node2dep_ids and not self.handmade_ap2dep_ids:
            return

        # collect all unique dep ids
        dep_ids = []
        for node_dep_ids in self.node2dep_ids.values():
            dep_ids += node_dep_ids
        for ap_dep_ids in self.handmade_ap2dep_ids.values():
            dep_ids += ap_dep_ids
        dep_ids = list(set(dep_ids))

        # collect all deps paths
        for node in self._graph_data:
            if not isinstance(node, SemNode):
                continue
            if node.id not in dep_ids or not node.name.endswith('.jar'):
                continue
            self.dep_paths[node.id] = Path(node.name.replace(self._BUILD_ROOT, ''))

    def _patch_node_annotation_processors(self, node: SemNode) -> None:
        """Path old AP semantics in one node"""
        for semantic in node.semantics:
            if semantic.sems[0] == self._OLD_AP_SEM:
                ap_paths = []
                ap_classes = semantic.sems[1:]
                for ap_class in ap_classes:
                    if ap_class in self.ap_class2path:
                        ap_path = self.ap_class2path[ap_class]
                        node_dep_paths = []
                        for dep_id in self.node2dep_ids[node.id]:
                            if dep_id not in self.dep_paths:
                                continue
                            dep_path = self.dep_paths[dep_id]
                            node_dep_paths.append(str(dep_path))
                            if dep_path.is_relative_to(Path(ap_path)):  # found dep with same base path
                                ap_path_by_dep = str(dep_path)
                                ap_paths.append(ap_path_by_dep)  # patch class by path
                                self._on_patch_annotation_processor(ap_class, ap_path_by_dep)
                                if dep_id in self.handmade_ap2dep_ids:  # Handmade AP
                                    for apdep_id in self.handmade_ap2dep_ids[dep_id]:  # add it deps as APs too
                                        if apdep_id in self.dep_paths:
                                            ap_dep_path = str(self.dep_paths[apdep_id])
                                            ap_paths.append(ap_dep_path)
                                break
                        else:
                            self.logger.error(
                                "Not found AP %s --> %s in dependencies of node %s[%d], skip it, all node dependencies:\n%s",
                                ap_class,
                                ap_path,
                                node.name,
                                node.id,
                                node_dep_paths,
                            )
                            # Skip usage unknown AP path
                    else:
                        self.logger.error("Not found path for AP class %s, skip it", ap_class)
                        # Skip usage unknown AP class
                # Replace old semantic with classes by new semantic with paths
                semantic.sems = [self._NEW_AP_SEM] + ap_paths

            elif semantic.sems[0] == self._KAPT_SEM:
                kapt_paths = []
                kapt_classpaths = semantic.sems[1:]
                node_dep_paths = []
                for kapt_classpath in kapt_classpaths:
                    for dep_id in self.node2dep_ids[node.id]:
                        if dep_id not in self.dep_paths:
                            continue
                        dep_path = self.dep_paths[dep_id]
                        node_dep_paths.append(dep_path)
                        if dep_path.is_relative_to(Path(kapt_classpath)):  # found dep with same base path
                            kapt_path_by_dep = str(dep_path)
                            kapt_paths.append(kapt_path_by_dep)
                            if dep_id in self.handmade_ap2dep_ids:  # Handmade AP
                                for apdep_id in self.handmade_ap2dep_ids[dep_id]:  # add it deps as APs too
                                    if apdep_id in self.dep_paths:
                                        kapt_dep_path = str(self.dep_paths[apdep_id])
                                        kapt_paths.append(kapt_dep_path)
                            break
                    else:
                        self.logger.error(
                            "Not found KAPT %s in dependencies of node %s[%d], skip it, all node dependencies:\n%s",
                            kapt_classpath,
                            node.name,
                            node.id,
                            node_dep_paths,
                        )
                        kapt_paths.append(kapt_classpath)  # use classpath as path
                    self._on_patch_kapt(kapt_classpath, kapt_paths[-1])
                # Replace semantic classpaths by deps paths
                semantic.sems = [self._KAPT_SEM] + kapt_paths

    def _on_patch_annotation_processor(self, ap_class: str, ap_path_by_dep: str) -> None:
        """Collect AP patching class -> path | paths"""
        if ap_class not in self.used_ap_class2path:
            self.used_ap_class2path[ap_class] = []
        if ap_path_by_dep not in self.used_ap_class2path[ap_class]:
            self.used_ap_class2path[ap_class].append(ap_path_by_dep)

    def _on_patch_kapt(self, kapt_classpath: str, kapt_path_by_dep: str) -> None:
        """Collect KAPT patching classpath | paths"""
        if kapt_classpath not in self.used_kapt_classpath2path:
            self.used_kapt_classpath2path[kapt_classpath] = []
        if kapt_path_by_dep not in self.used_kapt_classpath2path[kapt_classpath]:
            self.used_kapt_classpath2path[kapt_classpath].append(kapt_path_by_dep)

    def _check_annotation_processors_has_version(self, node: SemNode) -> None:
        for semantic in node.semantics:
            if semantic.sems[0] == self._NEW_AP_SEM:
                for ap_path in semantic.sems[1:]:
                    if (
                        not self.handmade_ap_re or not self.handmade_ap_re.match(ap_path)
                    ) and not _JavaSemGraph._is_path_has_version(ap_path):
                        self.logger.error(
                            "Using annotation processor without version %s in node %s", ap_path, node.as_dict()
                        )

    @staticmethod
    def _is_path_has_version(path: str) -> bool:
        return bool(re.fullmatch('^\\d+[.\\d]+.*$', Path(Path(path).parent).name))

    def _patch_jdk(self) -> None:
        """Patch JDK path and JDK version in graph"""
        self._before_any_patch()
        for node in self._graph_data:
            if not isinstance(node, SemNode) or not node.has_semantics() or not node.name.startswith(self._BUILD_ROOT):
                continue
            rel_target = Path(node.name.replace(self._BUILD_ROOT, '')).parent
            in_rel_targets = self.config.in_rel_targets(rel_target)
            for semantic in node.semantics:
                sem0 = semantic.sems[0]
                if sem0 in (self.JDK_VERSION_SEM, self.JDK_PATH_SEM, self.REQUIRED_JDK_SEM) and len(semantic.sems) > 1:
                    jdk_version = _JavaSemGraph._get_jdk_version(semantic.sems[1])
                    # don't load JDK for non-targets, fill by dummy string
                    jdk_path = self.get_jdk_path(jdk_version) if in_rel_targets else f"JDK_PATH_{jdk_version}"
                    if sem0 == self.JDK_PATH_SEM:
                        semantic.sems = [self.JDK_PATH_SEM, jdk_path]
                    else:
                        semantic.sems = [sem0, str(jdk_version)]
                    self._graph_patched = True

    def get_jdk_path(self, jdk_version: int, is_toolchain: bool = True) -> str:
        if self.config.params.force_jdk_version:
            jdk_version = int(self.config.params.force_jdk_version)
        if jdk_version in self._cached_jdk_paths:
            return self._cached_jdk_paths[jdk_version]

        platform = platform_matcher.my_platform()
        if platform.startswith('darwin'):
            jdk_home = Path.home() / "Library" / "Java" / "JavaVirtualMachines"
        elif platform.startswith('linux'):
            jdk_home = Path.home() / ".jdks"
        else:
            self.logger.error("Unknown platform %s, put JDK symlink to user home", platform)
            jdk_home = Path.home()
        jdk_path = jdk_home / ("arcadia-jdk-" + str(jdk_version))

        if not self.dont_symlink_jdk:
            try:
                jdk_real_path = Path(tools.tool(f'java{jdk_version}').replace('/bin/java', ''))
                if jdk_path.is_symlink() and jdk_path.resolve() != jdk_real_path:
                    jdk_path.unlink()  # remove invalid symlink to JDK
                if not jdk_path.exists():  # create new symlink to JDK
                    _SymlinkCollector.mkdir(jdk_path.parent)
                    jdk_path.symlink_to(jdk_real_path, target_is_directory=True)
            except Exception as e:
                self.logger.error("Can't find JDK %s in tools: %s", jdk_version, e)
                jdk_path = self.JDK_PATH_NOT_FOUND

        jdk_path = str(jdk_path)
        self._cached_jdk_paths[jdk_version] = jdk_path
        if is_toolchain and jdk_path != self.JDK_PATH_NOT_FOUND:
            self.jdk_paths[jdk_version] = jdk_path  # Public only valid jdk paths

        return jdk_path

    @staticmethod
    def _get_jdk_version(s: str) -> int:
        """Extract JDK version from resource var name"""
        if re.match('^\\d+$', s):  # prepare int version number in value
            return int(s)
        m = re.search('(?:JDK|jdk)(\\d+)', s)
        return int(m.group(1)) if m else 0

    def _get_mains(self) -> list[tuple[str, SemNode]]:
        """Collect all relative paths of main modules and they nodes"""
        mains: list[tuple[str, SemNode]] = []  # relative in arcadia for all main modules
        for node in self._graph_data:
            if not isinstance(node, SemNode) or not node.has_semantics():
                continue
            if node.semantics[0].sems[0] != self.JAR_SEM:  # non-main target module
                continue
            mains.append((str(Path(node.name.replace(self._BUILD_ROOT, '')).parent), node))
        mains.sort(key=lambda i: i[0], reverse=True)  # Long paths firstly
        return mains

    def _is_some_set(self, semantic: Semantic, generated: bool) -> bool:
        """Check this semantic contains generated source/resource set"""
        if len(semantic.sems) < 2:
            return False
        sem0 = semantic.sems[0]
        sem1 = semantic.sems[1]
        return (
            sem0
            in [
                self._SOURCE_SET_DIR_SEM,
                self._RESOURCE_SET_DIR_SEM,
            ]
        ) and ((self.str_export_root in sem1) == generated)

    def _get_main_src_path(self, node: SemNode) -> str:
        # Fill src_path from semantics by src/main/ or src/ or empty
        src_path = ""
        for s in node.semantics:
            if not self._is_some_set(s, generated=False):
                continue
            sem1 = s.sems[1]
            if sem1 == self._SRC_MAIN or sem1.startswith(self._SRC_MAIN + "/"):
                return self._SRC_MAIN + "/"
            elif sem1 == self._SRC or sem1.startswith(self._SRC + "/"):
                src_path = self._SRC + "/"
        return src_path

    def _get_test_src_path(self, node: SemNode, in_module_path: str) -> str:
        # Fill src_path from semantics by src/test/ or src/
        src_path = ""
        for s in node.semantics:
            if not self._is_some_set(s, generated=False):
                continue
            sum_path = (in_module_path + "/" if in_module_path else "") + s.sems[1]
            if sum_path == self._SRC_TEST or sum_path.startswith(self._SRC_TEST + "/"):
                return self._SRC_TEST + "/"
            elif sum_path == self._SRC or sum_path.startswith(self._SRC + "/"):
                src_path = self._SRC + "/"
        return src_path

    def _patch_exclude_targets(self) -> None:
        """Patch excluded targets in graph"""
        if not self.config.rel_exclude_targets:
            return
        self._before_any_patch()
        for node in self._graph_data:
            if not isinstance(node, SemNode) or not node.has_semantics():
                continue
            rel_target = Path(node.name.replace(self._BUILD_ROOT, '')).parent
            if not self.config.is_exclude_target(rel_target):
                continue
            has_ignored = False
            has_consumer_type = False
            for s in node.semantics:
                sem0 = s.sems[0]
                if sem0 == self._CONSUMER_TYPE_SEM:
                    has_consumer_type = True
                elif sem0 == self._IGNORED_SEM:
                    has_ignored = True
            if has_consumer_type:
                node.semantics.append(Semantic({Semantic.SEM: [self._CONSUMER_PREBUILT_SEM]}))
                self._graph_patched = True
            if not has_ignored:
                node.semantics.append(Semantic({Semantic.SEM: [self._IGNORED_SEM]}))
                self._graph_patched = True
