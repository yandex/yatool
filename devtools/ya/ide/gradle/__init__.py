import os
import sys
import logging
import shutil
import subprocess
import re
from collections.abc import Iterable
from pathlib import Path

from devtools.ya.core import config as core_config, yarg, stage_tracer
from devtools.ya.build import build_opts, graph as build_graph, ya_make
from devtools.ya.build.sem_graph import SemLang, SemConfig, SemNode, SemDep, SemGraph
from yalibrary import platform_matcher, tools
from exts import hashing
from devtools.ya.yalibrary import sjson
import xml.etree.ElementTree as eTree


class YaIdeGradleException(Exception):
    mute = True


class _JavaSemConfig(SemConfig):
    """Check and use command line options for configure roots and flags"""

    GRADLE_PROPS = 'gradle.properties'  # Gradle properties filename auto detected by Gradle
    GRADLE_PROPS_FILE: Path = Path.home() / '.gradle' / GRADLE_PROPS  # User Gradle properties file
    GRADLE_REQUIRED_PROPS: tuple[str] = (
        'bucketUsername',
        'bucketPassword',
        'systemProp.gradle.wrapperUser',
        'systemProp.gradle.wrapperPassword',
    )

    EXPORT_ROOT_BASE: Path = Path(core_config.misc_root()) / 'gradle'  # Base folder of all export roots

    def __init__(self, params):
        if platform_matcher.is_windows():
            raise YaIdeGradleException("Windows is not supported in ya ide gradle")
        super().__init__(SemLang.JAVA(), params)
        self.logger = logging.getLogger(type(self).__name__)
        self.settings_root: Path = self._get_settings_root()
        if not self.params.remove:
            self._check_gradle_props()

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
                        'For use [ya ide gradle] REQUIRED access from Gradle to Bucket [http://bucket.yandex-team.ru/]',
                        'Please, read more about work with Bucket https://docs.yandex-team.ru/bucket',
                        'and authentication for Gradle https://docs.yandex-team.ru/bucket/gradle#autentifikaciya',
                        'Token can be taken from here https://oauth.yandex-team.ru/authorize?response_type=token&client_id=bf8b6a8a109242daaf62bce9d6609b3b',
                        '',
                        f'Now Gradle properties file {_JavaSemConfig.GRADLE_PROPS_FILE} is invalid:',
                        *errors,
                    ]
                )
            )
        if not self.params.collect_contribs:
            self.logger.warning(
                "You have selected the mode without collecting contribs to jar files, to build successfully in Gradle, check bucket repository settings and access rights"
            )

    def _get_export_root(self) -> Path:
        """Create export_root path by hash of targets"""
        targets_hash = hashing.fast_hash(':'.join(sorted(self.params.abs_targets)))
        export_root = _JavaSemConfig.EXPORT_ROOT_BASE / targets_hash
        self.logger.info("Export root: %s", export_root)
        return export_root

    def _get_settings_root(self) -> Path:
        """Create settings_root path by options and targets"""
        settings_root = Path(self.params.abs_targets[0])
        if self.params.settings_root:
            settings_root = self.arcadia_root / Path(self.params.settings_root)
        elif len(self.params.abs_targets) > 1:
            cwd = Path.cwd()
            if cwd.is_relative_to(self.arcadia_root) and cwd != self.arcadia_root:
                settings_root = cwd
        self.logger.info("Settings root: %s", settings_root)
        if not settings_root.exists() or not settings_root.is_dir():
            raise YaIdeGradleException('Not found settings root directory')
        return settings_root

    def in_rel_targets(self, rel_target: Path) -> bool:
        for conf_rel_target in self.params.rel_targets:
            if rel_target.is_relative_to(Path(conf_rel_target)):
                return True
        return False


class _YaSettings:
    """Save command and cwd to ya-settings.xml"""

    YA_SETTINGS_XML = 'ya-settings.xml'

    def __init__(self, java_sem_config: _JavaSemConfig):
        self.config: _JavaSemConfig = java_sem_config

    def save(self) -> None:
        self._write_xml(self._make_xml(), self.config.export_root / self.YA_SETTINGS_XML)

    @classmethod
    def _make_xml(cls) -> eTree.Element:
        xml_root = eTree.Element('root')
        cmd = eTree.SubElement(xml_root, 'cmd')
        for arg in sys.argv:
            eTree.SubElement(cmd, 'part').text = arg
        eTree.SubElement(xml_root, 'cwd').text = str(Path.cwd())
        return xml_root

    @classmethod
    def _write_xml(cls, xml_root: eTree.Element, path: Path) -> None:
        cls._elem_indent(xml_root)
        with path.open('wb') as f:
            eTree.ElementTree(xml_root).write(f, encoding="utf-8")

    @classmethod
    def _elem_indent(cls, elem, level=0) -> None:
        indent = "\n" + level * " " * 4
        if len(elem):
            if not elem.text or not elem.text.strip():
                elem.text = indent + " " * 4
            if not elem.tail or not elem.tail.strip():
                elem.tail = indent
            for elem in elem:
                cls._elem_indent(elem, level + 1)
            if not elem.tail or not elem.tail.strip():
                elem.tail = indent
        else:
            if level and (not elem.tail or not elem.tail.strip()):
                elem.tail = indent


class _SymlinkCollector:
    """Iterate on settings and build root and call collect function for every place, where symlinks waited"""

    SETTINGS_FILES: tuple[str] = (
        "settings.gradle.kts",
        "gradlew",
        "gradlew.bat",
        _YaSettings.YA_SETTINGS_XML,
        _JavaSemConfig.GRADLE_PROPS,
    )  # Files for symlink to settings root
    SETTINGS_MKDIRS: tuple[str] = (".gradle", ".idea", ".kotlin")  # Folders for creating at settings root
    SETTINGS_DIRS: tuple[str] = list(SETTINGS_MKDIRS) + ["gradle"]  # Folders for symlink to settings root

    BUILD_SKIP_ROOT_DIRS: tuple[str] = list(SETTINGS_DIRS) + [
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
            _SymlinkCollector.mkdir(self.config.export_root / mkdir)
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

    @staticmethod
    def mkdir(path: Path) -> None:
        path.mkdir(0o755, parents=True, exist_ok=True)

    @staticmethod
    def resolve(path: Path, path_root: Path = None, export_root: Path = None) -> tuple[bool, Path]:
        """If symlink, resolve path, else return path as is"""
        if not path.is_symlink():
            return True, path
        try:
            resolved_path = path.resolve(True)
            if path_root is not None and export_root is not None and resolved_path.is_relative_to(export_root):
                tail = str(path.relative_to(path_root))  # Tail of path relative to root
                if not str(resolved_path).endswith(tail):  # Resolved must has same tail
                    # else it invalid symlink
                    path.unlink()  # remove invalid symlink
                    return False, path
            return True, resolved_path
        except Exception:
            path.unlink()  # remove invalid symlink
            return False, path


class _ExistsSymlinkCollector(_SymlinkCollector):
    """Collect exists symlinks for remove later"""

    _SYMLINKS_FILE = 'symlinks.json'

    def __init__(self, java_sem_config: _JavaSemConfig):
        super().__init__(java_sem_config)
        self.logger = logging.getLogger(type(self).__name__)
        self.symlinks: dict[Path, Path] = {}

    def collect(self) -> None:
        """Collect already exists symlinks"""
        if not self.config.export_root.exists():
            return

        try:
            if self._load():
                return
        except Exception as e:
            self.logger.error("Can't load symlinks from file %s: %s", self._symlinks_path, e)

        for export_file, arcadia_file in self.collect_symlinks():
            if self._check_symlink(arcadia_file, export_file):
                self.add_symlink(arcadia_file, export_file)

    def add_symlink(self, arcadia_file: Path, export_file: Path) -> None:
        self.symlinks[arcadia_file] = export_file

    def del_symlink(self, arcadia_file: Path) -> None:
        del self.symlinks[arcadia_file]

    def save(self) -> None:
        if not self.config.export_root.exists():
            return
        symlinks_path = self._symlinks_path
        symlinks: dict[str, str] = {
            str(arcadia_file): str(export_file) for arcadia_file, export_file in self.symlinks.items()
        }
        with symlinks_path.open('wb') as f:
            sjson.dump(symlinks, f)

    def _load(self) -> bool:
        symlinks_path = self._symlinks_path
        if not symlinks_path.exists():
            return False
        with symlinks_path.open('rb') as f:
            symlinks: dict[str, str] = sjson.load(f)
        self.symlinks = {}
        for arcadia_file, export_file in symlinks.items():
            arcadia_file = Path(arcadia_file)
            export_file = Path(export_file)
            if self._check_symlink(arcadia_file, export_file):
                self.add_symlink(arcadia_file, export_file)
        return True

    def _check_symlink(self, arcadia_file: Path, export_file: Path) -> bool:
        """Check symlink exists, remove invalid symlinks"""
        is_valid, to_path = _SymlinkCollector.resolve(arcadia_file)
        return is_valid and to_path == export_file.absolute()

    @property
    def _symlinks_path(self) -> Path:
        """Make filename for store symlinks"""
        return self.config.export_root / self._SYMLINKS_FILE


class _RemoveSymlinkCollector(_SymlinkCollector):
    """Collect for remove symlinks"""

    def __init__(self, exists_symlinks: _ExistsSymlinkCollector):
        super().__init__(exists_symlinks.config)
        self.logger = logging.getLogger(type(self).__name__)
        self.symlinks: dict[Path, Path] = exists_symlinks.symlinks.copy()
        self.exists_symlinks: _ExistsSymlinkCollector = exists_symlinks

    def remove(self) -> None:
        """Remove symlinks from arcadia files to export files"""
        for arcadia_file, export_file in self.symlinks.items():
            try:
                arcadia_file.unlink()
                self.exists_symlinks.del_symlink(arcadia_file)  # remove deleted from exists
            except Exception as e:
                self.logger.warning(
                    "Can't remove symlink '%s' -> '%s': %s", arcadia_file, export_file, e, exc_info=True
                )

    def add_symlink(self, arcadia_file: Path, export_file: Path) -> None:
        self.symlinks[arcadia_file] = export_file

    def del_symlink(self, arcadia_file: Path) -> None:
        del self.symlinks[arcadia_file]

    def remove_invalid_symlinks(self):
        _RemoveSymlinkCollector._remove_invalid_symlinks(self.config.settings_root, True, self.config.export_root)
        for rel_target in self.config.params.rel_targets:
            _RemoveSymlinkCollector._remove_invalid_symlinks(
                self.config.arcadia_root / rel_target, False, self.config.export_root
            )

    @staticmethod
    def _remove_invalid_symlinks(dirtree: Path, top_only: bool = False, export_root: Path = None) -> None:
        """Remove all invalid symlinks"""
        for walk_root, dirs, files in dirtree.walk():
            for item in dirs + files:
                _SymlinkCollector.resolve(walk_root / item, dirtree, export_root)
            if top_only:
                dirs.clear()


class _NewSymlinkCollector(_SymlinkCollector):
    """Collect new symlinks for create later, exclude already exists"""

    def __init__(self, exists_symlinks: _ExistsSymlinkCollector, remove_symlinks: _RemoveSymlinkCollector):
        super().__init__(exists_symlinks.config)
        self.logger = logging.getLogger(type(self).__name__)
        self.exists_symlinks: _ExistsSymlinkCollector = exists_symlinks
        self.remove_symlinks: _RemoveSymlinkCollector = remove_symlinks
        self.symlinks: dict[Path, Path] = {}
        self.has_errors: bool = False

    def collect(self):
        """Collect new symlinks for creating, skip already exists symlinks"""
        for export_file, arcadia_file in self.collect_symlinks():
            if arcadia_file in self.remove_symlinks.symlinks:
                # Already exists, don't remove it
                self.remove_symlinks.del_symlink(arcadia_file)
            elif not arcadia_file.exists():
                self.symlinks[arcadia_file] = export_file
            elif arcadia_file.is_symlink():
                is_valid, to_path = _SymlinkCollector.resolve(arcadia_file)
                if is_valid:
                    if (to_path != export_file.absolute()) and to_path.is_relative_to(_JavaSemConfig.EXPORT_ROOT_BASE):
                        self.logger.error(
                            "Can't create symlink %s -> %s, already symlink to another Gradle project -> %s",
                            arcadia_file,
                            export_file,
                            to_path,
                        )
                        self.has_errors = True
                else:
                    self.symlinks[arcadia_file] = export_file  # require create new symlink

    def create(self) -> None:
        """Create symlinks from arcadia files to export files"""
        for arcadia_file, export_file in self.symlinks.items():
            try:
                arcadia_file.symlink_to(export_file, export_file.is_dir())
                self.exists_symlinks.add_symlink(arcadia_file, export_file)  # add created symlink as exists
            except Exception as e:
                self.logger.error("Can't create symlink '%s' -> '%s': %s", arcadia_file, export_file, e, exc_info=True)
                self.has_errors = True


class _JavaSemGraph(SemGraph):
    """Creating and reading sem-graph"""

    JDK_PATH_SEM = 'jdk_path'
    JDK_VERSION_SEM = 'jdk_version'

    JDK_PATH_NOT_FOUND = 'NOT_FOUND'  # Magic const for not found JDK path
    BUILD_ROOT = '$B/'  # Build root in graph

    _OLD_AP_SEM = 'annotation_processors'
    _NEW_AP_SEM = 'use_annotation_processor'
    _KAPT_SEM = 'kapt-classpaths'

    _FOREIGN_PLATFORM_TARGET_TYPENAME = 'NEvent.TForeignPlatformTarget'
    _FOREIGN_IDE_DEPEND_PLATFORM = 3

    def __init__(self, config: _JavaSemConfig):
        super().__init__(config, skip_invalid=True)
        self.logger = logging.getLogger(type(self).__name__)
        self._graph_data: list[SemNode | SemDep] = []
        self._graph_patched = False
        self.used_ap_class2path: dict[str, list[str]] = {}
        self.used_kapt_classpath2path: dict[str, list[str]] = {}
        self.use_ap_node_ids: list[int] = []
        self.node2dep_ids: dict[int, list[int]] = {}
        self.dep_paths: dict[int, Path] = {}
        self._cached_jdk_paths = {}
        self.jdk_paths: dict[int, str] = {}
        self.foreign_targets: list[str] = []
        self.gradle_jdk_version: int = 17  # by default use JDK 17 for Gradle
        if self.config.params.force_jdk_version and int(self.config.params.force_jdk_version) > self.gradle_jdk_version:
            self.gradle_jdk_version = int(self.config.params.force_jdk_version)
        self.dont_symlink_jdk: bool = False  # Don't create symlinks to JDK (for tests)

    def make(self, **kwargs) -> None:
        """Make sem-graph file by ymake"""
        foreign_targets = []

        def listener(event) -> None:
            if not isinstance(event, dict):
                return
            if event.get('Type') == 'Error':
                self.logger.error("%s", event)
            if (
                event.get('_typename') == self._FOREIGN_PLATFORM_TARGET_TYPENAME
                and event.get('Platform') == self._FOREIGN_IDE_DEPEND_PLATFORM
            ):
                foreign_targets.append(event['Dir'])

        self.dont_symlink_jdk = kwargs.pop('dont_symlink_jdk', False)

        super().make(
            **kwargs,
            ev_listener=listener,
            dump_raw_graph=self.config.ymake_root / "raw_graph",
            foreign_on_nosem=True,
            debug_options=self.config.params.debug_options,
            dump_file=self.config.params.dump_file_path,
            warn_mode=self.config.params.warn_mode,
            dump_ymake_stderr=self.config.params.dump_ymake_stderr,
        )
        if foreign_targets:
            self.foreign_targets = list(set(foreign_targets))
            self.logger.info("Foreign targets: %s", self.foreign_targets)
        if 'dont_patch_graph' not in kwargs:
            self._patch_graph()

    def get_rel_targets(self) -> list[(Path, bool)]:
        """Get list of rel_targets from sem-graph with is_contrib flag for each"""
        rel_targets = []
        for node in self.read():
            if not isinstance(node, SemNode):
                continue  # interest only nodes
            if not node.name.startswith(self.BUILD_ROOT) or not node.name.endswith(
                '.jar'
            ):  # Search only *.jar with semantics
                continue
            rel_target = Path(node.name.replace(self.BUILD_ROOT, '')).parent  # Relative target - directory of *.jar
            is_contrib = False
            for semantic in node.semantics:
                if semantic.sems == ['consumer-type', 'contrib']:
                    is_contrib = True
                    break
            rel_targets.append((rel_target, is_contrib))
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
        self._patch_annotation_processors()
        self._patch_jdk()
        if self._graph_patched:
            self._update_graph()

    def _update_graph(self) -> None:
        data: list[dict] = []
        for item in self._graph_data:
            data.append(item.as_dict())
        self.update(data)

    def _patch_annotation_processors(self) -> None:
        """Patch AP semantics in graph"""
        self._configure_patch_annotation_processors()
        self._get_graph_data_and_find_annotation_processors()
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

    def get_configs_dir(self) -> Path:
        """Get directory with ya ide gradle configs"""
        return self.config.arcadia_root / "build" / "yandex_specific" / "gradle"

    def _configure_patch_annotation_processors(self) -> None:
        """Read mapping AP class -> path from configure"""
        annotation_processors_file = self.get_configs_dir() / "annotation_processors.json"
        if not annotation_processors_file.exists():
            raise YaIdeGradleException(f"Not found {annotation_processors_file}")
        with annotation_processors_file.open('rb') as f:
            self.ap_class2path = sjson.load(f)

    def _get_graph_data_and_find_annotation_processors(self) -> None:
        """Find nodes with AP semantics (old or new), collect dep ids for old AP semantics"""
        data: list[SemNode | SemDep] = []
        for item in self.read(all_nodes=True):
            if isinstance(item, SemDep):
                if item.from_id in self.node2dep_ids:  # collect dep ids for patching AP
                    self.node2dep_ids[item.from_id].append(item.to_id)
            if not isinstance(item, SemNode):
                data.append(item)  # non-node direct append to patched graph as is
                continue
            node = item
            if node.has_semantics():
                for semantic in node.semantics:
                    sem0 = semantic.sems[0]
                    if sem0 == self._OLD_AP_SEM or sem0 == self._KAPT_SEM:
                        self.node2dep_ids[node.id] = []  # require collect deps for patch AP classes to AP paths
                    elif sem0 == self._NEW_AP_SEM:
                        self.use_ap_node_ids.append(node.id)  # collect node ids to check for versions of all AP
            data.append(node)
        self._graph_data = data

    def _do_patch_annotation_processors(self) -> None:
        """Patch AP semantics in graph and check all AP with versions"""
        if not self.use_ap_node_ids and not self.node2dep_ids:
            return

        self._fill_dep_paths()

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
        if not self.node2dep_ids:
            return

        # collect all unique dep ids
        dep_ids = []
        for node_dep_ids in self.node2dep_ids.values():
            dep_ids += node_dep_ids
        dep_ids = list(set(dep_ids))

        # collect all deps paths
        for node in self._graph_data:
            if not isinstance(node, SemNode):
                continue
            if node.id not in dep_ids:
                continue
            self.dep_paths[node.id] = Path(node.name.replace(self.BUILD_ROOT, ''))

    def _patch_node_annotation_processors(self, node: SemNode) -> None:
        """Path old AP semantics in one node"""
        for semantic in node.semantics:
            if semantic.sems[0] == self._OLD_AP_SEM:
                ap_paths = []
                ap_classes = semantic.sems[1:]
                for ap_class in ap_classes:
                    if ap_class in self.ap_class2path:
                        ap_path = self.ap_class2path[ap_class]
                        for dep_id in self.node2dep_ids[node.id]:
                            dep_path = self.dep_paths[dep_id]
                            if dep_path.is_relative_to(Path(ap_path)):  # found dep with same base path
                                ap_path_by_dep = str(dep_path)
                                ap_paths.append(ap_path_by_dep)  # patch class by path
                                self._on_patch_annotation_processor(ap_class, ap_path_by_dep)
                                break
                        else:
                            self.logger.error(
                                "Not found AP %s --> %s in dependencies of node %s[%d], skip it, all node dependencies:\n%s",
                                ap_class,
                                ap_path,
                                node.name,
                                node.id,
                                [self.dep_paths[dep_id] for dep_id in self.node2dep_ids[node.id]],
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
                for kapt_classpath in kapt_classpaths:
                    for dep_id in self.node2dep_ids[node.id]:
                        dep_path = self.dep_paths[dep_id]
                        if dep_path.is_relative_to(Path(kapt_classpath)):  # found dep with same base path
                            kapt_path_by_dep = str(dep_path)
                            kapt_paths.append(kapt_path_by_dep)
                            break
                    else:
                        self.logger.error(
                            "Not found KAPT %s in dependencies of node %s[%d], skip it, all node dependencies:\n%s",
                            kapt_classpath,
                            node.name,
                            node.id,
                            [self.dep_paths[dep_id] for dep_id in self.node2dep_ids[node.id]],
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
                    if not _JavaSemGraph._is_path_has_version(ap_path):
                        self.logger.error(
                            "Using annotation processor without version %s in node %s", ap_path, node.as_dict()
                        )

    @staticmethod
    def _is_path_has_version(path: str) -> bool:
        return bool(re.fullmatch('^\\d+[.\\d]+.*$', Path(Path(path).parent).name))

    def _patch_jdk(self) -> None:
        """Patch JDK path and JDK version in graph"""
        for node in self._graph_data:
            if not isinstance(node, SemNode) or not node.has_semantics() or not node.name.startswith(self.BUILD_ROOT):
                continue
            rel_target = Path(node.name.replace(self.BUILD_ROOT, '')).parent
            in_rel_targets = self.config.in_rel_targets(rel_target)
            for semantic in node.semantics:
                sem0 = semantic.sems[0]
                if sem0 == self.JDK_VERSION_SEM and len(semantic.sems) > 1:
                    jdk_version = _JavaSemGraph._get_jdk_version(semantic.sems[1])
                    semantic.sems = [self.JDK_VERSION_SEM, str(jdk_version)]
                    self._graph_patched = True
                elif sem0 == self.JDK_PATH_SEM and len(semantic.sems) > 1:
                    jdk_version = _JavaSemGraph._get_jdk_version(semantic.sems[1])
                    # don't load JDK for non-targets, fill by dummy string
                    jdk_path = self.get_jdk_path(jdk_version) if in_rel_targets else f"JDK_PATH_{jdk_version}"
                    semantic.sems = [self.JDK_PATH_SEM, jdk_path]
                    self._graph_patched = True

    def get_jdk_path(self, jdk_version: int) -> str:
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
        if jdk_path != self.JDK_PATH_NOT_FOUND:
            self.jdk_paths[jdk_version] = jdk_path  # Public only valid jdk paths

            if jdk_version > self.gradle_jdk_version:
                self.gradle_jdk_version = jdk_version  # Use for Gradle max JDK version in graph
        return jdk_path

    @staticmethod
    def _get_jdk_version(s: str) -> int:
        """Extract JDK version from resource var name"""
        m = re.search('(?:JDK|jdk)(\\d+)', s)
        return int(m.group(1)) if m else 0


class _Exporter:
    """Generating files to export root"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph
        self.project_name: str = None
        self.attrs_for_all_templates: list[str] = []

    def export(self) -> None:
        """Generate files from sem-graph by yexport"""
        self._make_project_name()
        self._make_project_gradle_props()
        self._apply_force_jdk_version()
        self._fill_common_dir()
        self._make_yexport_toml()
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
        const_gradle_properties_file = self.sem_graph.get_configs_dir() / ("const." + _JavaSemConfig.GRADLE_PROPS)
        project_gradle_properties = []
        if const_gradle_properties_file.exists():
            with const_gradle_properties_file.open('r') as f:
                project_gradle_properties += f.read().split("\n")
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
                        f'build_contribs = {'true' if self.config.params.collect_contribs else 'false'}',
                        f'disable_errorprone = {'true' if self.config.params.disable_errorprone else 'false'}',
                        f'disable_lombok_plugin = {'true' if self.config.params.disable_lombok_plugin else 'false'}',
                        *self.attrs_for_all_templates,
                        '',
                        '[add_attrs.target]',
                        *self.attrs_for_all_templates,
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


class _Builder:
    """Build required targets"""

    def __init__(self, java_sem_config: _JavaSemConfig, java_sem_graph: _JavaSemGraph):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.sem_graph: _JavaSemGraph = java_sem_graph

    def build(self) -> None:
        """Extract build targets from sem-graph and build they"""
        try:
            build_rel_targets = list(set(self.sem_graph.get_run_java_program_rel_targets()))
            rel_targets = self.sem_graph.get_rel_targets()
            for rel_target, is_contrib in rel_targets:
                if self.config.in_rel_targets(rel_target):
                    # Skip target, already in input targets
                    continue
                elif self.config.params.collect_contribs or not is_contrib:
                    # Build all non-input or not contrib targets
                    build_rel_targets.append(rel_target)
        except Exception as e:
            raise YaIdeGradleException(
                f'Fail extract build targets from sem-graph {self.sem_graph.sem_graph_file}: {e}'
            ) from e

        if build_rel_targets:
            self._build_rel_targets(build_rel_targets)

        if self.config.params.build_foreign and self.sem_graph.foreign_targets:
            self._build_rel_targets(self.sem_graph.foreign_targets, True)

    def _build_rel_targets(self, build_rel_targets: list[str], build_all_langs: bool = False) -> None:
        """Build all relative targets by one graph, build_all_langs control only java targets or all languages targets"""
        import app_ctx

        try:
            if build_all_langs:
                opts = yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True, build_type='release'))
            else:
                ya_make_opts = yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True, build_type='debug'))
                opts = yarg.merge_params(ya_make_opts.initialize(self.config.params.ya_make_extra))
                opts.dump_sources = True

            arcadia_root = self.config.arcadia_root

            opts.bld_dir = self.config.params.bld_dir
            opts.arc_root = str(arcadia_root)
            opts.bld_root = self.config.params.bld_root

            opts.rel_targets = []
            opts.abs_targets = []
            for build_rel_target in build_rel_targets:  # Add all targets for build simultaneously
                opts.rel_targets.append(str(build_rel_target))
                opts.abs_targets.append(str(arcadia_root / build_rel_target))

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


class _Remover:
    """Remove all symlinks and export root"""

    def __init__(self, java_sem_config: _JavaSemConfig, remove_symlinks: _RemoveSymlinkCollector):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.remove_symlinks: _RemoveSymlinkCollector = remove_symlinks

    def remove(self) -> None:
        """Remove all exists symlinks and then remove export root"""
        if self.remove_symlinks.symlinks:
            self.logger.info("Remove %d symlinks from arcadia to export root", len(self.remove_symlinks.symlinks))
            self.remove_symlinks.remove()
        if self.config.export_root.exists():
            try:
                self.logger.info("Remove export root %s", self.config.export_root)
                shutil.rmtree(self.config.export_root)
            except Exception as e:
                self.logger.warning("While removing %s: %s", self.config.export_root, e, exc_info=True)
        else:
            self.logger.info("Export root %s already not found", self.config.export_root)


def _collect_symlinks(config: _JavaSemConfig) -> tuple[_ExistsSymlinkCollector, _RemoveSymlinkCollector]:
    """Collect exists and invalid symlinks, remove invalid symlinks"""
    exists_symlinks = _ExistsSymlinkCollector(config)
    exists_symlinks.collect()
    remove_symlinks = _RemoveSymlinkCollector(exists_symlinks)
    remove_symlinks.remove_invalid_symlinks()
    return exists_symlinks, remove_symlinks


def do_gradle(params):
    """Real handler of `ya ide gradle`"""
    do_gradle_stage = stage_tracer.get_tracer("gradle").start('do_gradle')

    try:
        config = _JavaSemConfig(params)

        exists_symlinks, remove_symlinks = _collect_symlinks(config)

        if config.params.remove or config.params.reexport:
            remover = _Remover(config, remove_symlinks)
            remover.remove()
            if not config.params.reexport:
                return
            exists_symlinks, remove_symlinks = _collect_symlinks(config)

        sem_graph = _JavaSemGraph(config)
        sem_graph.make()

        exporter = _Exporter(config, sem_graph)
        exporter.export()

        ya_settings = _YaSettings(config)
        ya_settings.save()

        new_symlinks = _NewSymlinkCollector(exists_symlinks, remove_symlinks)
        new_symlinks.collect()

        if new_symlinks.has_errors:
            raise YaIdeGradleException('Some errors during creating symlinks, read the logs for more information')

        remove_symlinks.remove()
        new_symlinks.create()
        exists_symlinks.save()

        builder = _Builder(config, sem_graph)
        builder.build()

    finally:
        do_gradle_stage.finish()
