import copy
import os
import logging
import shutil
import tempfile
from pathlib import Path
from typing import Iterable

from devtools.ya.build.ymake2 import ymake_sem_graph
from yalibrary import tools
from devtools.ya.yalibrary import sjson
from devtools.ya.build import build_facade
from devtools.ya.core import config as core_config


class SemException(Exception):
    pass


class SemLang:
    """Languages supported by sem-graph"""

    def __init__(self, lang: str, flags: dict[str, str], extra: list[str]):
        self.lang: str = lang
        self.flags: dict[str, str] = flags
        self.extra: list[str] = extra

    @classmethod
    def JAVA(cls) -> "SemLang":
        return cls(
            lang="java",
            flags={
                'YA_IDE_GRADLE': 'yes',
                'EXPORT_GRADLE': 'yes',
                'BUILD_LANGUAGES': 'JAVA',  # KOTLIN == JAVA
            },
            extra=['-DBUILD_LANGUAGES=JAVA'],
        )

    @classmethod
    def CPP(cls) -> "SemLang":
        return cls(
            lang="cpp",
            flags={
                'EXPORT_CMAKE': 'yes',
                'BUILD_LANGUAGES': 'CPP',  # C == CPP
            },
            extra=['-DBUILD_LANGUAGES=CPP'],
        )


class SemConfig:
    """Configure making sem-graph and exporting with it"""

    YMAKE_DIR = "ymake"  # Subfolder of export root for ymake files

    def __init__(self, lang: SemLang, params):
        self.logger = logging.getLogger(type(self).__name__)
        self.lang = lang
        self.params = copy.copy(params)
        self.arcadia_root: Path = Path(self.params.arc_root)
        self._prepare_targets()
        self.export_root: Path = self._get_export_root()
        self.ymake_root: Path = self.export_root / SemConfig.YMAKE_DIR
        self._ymake_bin_cache = None
        if hasattr(self.params, 'ymake_bin'):
            self._ymake_bin_cache = self.params.ymake_bin
        self._yexport_bin_cache = None
        if hasattr(self.params, 'yexport_bin'):
            self._yexport_bin_cache = self.params.yexport_bin

        self.params.flags.update(
            {  # Common flags for all languages
                'EXPORTED_BUILD_SYSTEM_SOURCE_ROOT': str(self.arcadia_root),
                'EXPORTED_BUILD_SYSTEM_BUILD_ROOT': str(self.export_root),
                'TRAVERSE_RECURSE': 'yes',
                'TRAVERSE_RECURSE_FOR_TESTS': 'yes',
                'USE_PREBUILT_TOOLS': 'no',
                'FAIL_MODULE_CMD': 'IGNORED',
            }
        )

        if self.lang.flags:
            self.params.flags.update(self.lang.flags)

        if self.lang.extra and hasattr(self.params, 'ya_make_extra'):
            self.params.ya_make_extra += self.lang.extra

    def _prepare_targets(self) -> None:
        """Add current directory to input targets, if targets empty"""
        if not self.params.abs_targets:
            abs_target = Path.cwd().resolve()
            self.params.abs_targets.append(str(abs_target))
            self.params.rel_targets.append(os.path.relpath(abs_target, self.arcadia_root))
        self.logger.info("Targets: %s", self.params.rel_targets)

    def _get_export_root(self) -> Path:
        """As a rule must overwrite by child class"""
        export_root = Path(tempfile.mkdtemp('sem'))
        self.logger.info("Export root: %s", export_root)
        return export_root

    @property
    def ymake_bin(self) -> str:
        """Lazy get ymake_bin path"""
        if not self._ymake_bin_cache:
            self._ymake_bin_cache = tools.tool('ymake')
        return self._ymake_bin_cache

    @property
    def yexport_bin(self) -> str:
        """Lazy get yexport_bin path"""
        if not self._yexport_bin_cache:
            self._yexport_bin_cache = tools.tool('yexport')
        return self._yexport_bin_cache


class Semantic:
    """One semantic in sem-graph node"""

    SEM = 'sem'

    def __init__(self, data: dict):
        if Semantic.SEM not in data:
            raise SemException(f"Not found '{Semantic.SEM}' in semantic")
        data_sem = data[Semantic.SEM]
        if not isinstance(data_sem, list):
            raise SemException(f"Field '{Semantic.SEM}' in semantic is not list")
        if not data_sem:
            raise SemException(f"Field '{Semantic.SEM}' in semantic is empty list")
        if not data_sem[0]:
            raise SemException(f"Empty first item in '{Semantic.SEM}' in semantic")
        self.sems: list[str] = [str(s) for s in data_sem]

    def as_dict(self) -> dict:
        return {
            Semantic.SEM: self.sems,
        }


class SemNode:
    """Node of sem-graph"""

    ID = 'Id'
    NAME = 'Name'
    NODE_TYPE = 'NodeType'
    TAG = 'Tag'
    TOOLS = 'Tools'
    TESTS = 'Tests'
    SEMANTICS = 'semantics'

    def __init__(self, data: dict, skip_invalid: bool = False, logger: logging.Logger = None):
        if SemNode.ID not in data:
            raise SemException(f"Not found '{SemNode.ID}' in node")
        self.id: int = SemNode.take_id(data)
        if SemNode.NAME not in data:
            raise SemException(f"Not found '{SemNode.NAME}' in node")
        self.name: str = SemNode.take_name(data)
        if SemNode.NODE_TYPE not in data:
            raise SemException(f"Not found '{SemNode.NODE_TYPE}' in node")
        self.type: str = str(data[SemNode.NODE_TYPE])
        self.tag: str = str(data[SemNode.TAG]) if SemNode.TAG in data else None
        self.tools: list[int] = [int(node_id) for node_id in data[SemNode.TOOLS]] if SemNode.TOOLS in data else None
        self.tests: list[int] = [int(node_id) for node_id in data[SemNode.TESTS]] if SemNode.TESTS in data else None
        self.semantics = None
        if SemNode.SEMANTICS in data:
            data_semantics = data[SemNode.SEMANTICS]
            if not isinstance(data_semantics, list):
                raise SemException(f"Field '{SemNode.SEMANTICS}' is not list")
            if not data_semantics:
                raise SemException(f"Field '{SemNode.SEMANTICS}' is empty list")
            try:
                self.semantics: list[Semantic] = []
                for data_semantic in data_semantics:
                    self.semantics.append(Semantic(data_semantic))
            except SemException as e:
                if skip_invalid:
                    if logger is not None:
                        logger.warning("Skip invalid semantic %s of node %s: %s", data_semantic, data, e)
                else:
                    raise SemException(f'Fail parse semantic {data_semantic}: {e}') from e

    def has_semantics(self) -> bool:
        return self.semantics is not None

    @staticmethod
    def take_id(data: dict) -> int:
        return int(data[SemNode.ID])

    @staticmethod
    def take_name(data: dict) -> str:
        return str(data[SemNode.NAME])

    def as_dict(self) -> dict:
        r = {
            SemGraph.DATATYPE: SemGraph.DATATYPE_NODE,
            SemNode.ID: self.id,
            SemNode.NAME: self.name,
            SemNode.NODE_TYPE: self.type,
        }
        if self.tag:
            r[SemNode.TAG] = self.tag
        if self.tools:
            r[SemNode.TOOLS] = self.tools
        if self.tests:
            r[SemNode.TESTS] = self.tests
        if self.semantics:
            r[SemNode.SEMANTICS] = [semantic.as_dict() for semantic in self.semantics]
        return r


class SemDep:
    """Dependence of sem-graph"""

    FROM_ID = 'FromId'
    TO_ID = 'ToId'
    DEP_TYPE = 'DepType'
    IS_CLOSURE = 'IsClosure:bool'
    EXCLUDES = 'Excludes:[NodeId]'

    def __init__(self, data: dict, skip_invalid: bool = False, logger: logging.Logger = None):
        if SemDep.FROM_ID not in data:
            raise SemException(f"Not found '{SemDep.FROM_ID}' in dependence")
        self.from_id: int = int(data[SemDep.FROM_ID])
        if SemDep.TO_ID not in data:
            raise SemException(f"Not found '{SemDep.TO_ID}' in dependence")
        self.to_id: int = int(data[SemDep.TO_ID])
        if SemDep.DEP_TYPE not in data:
            raise SemException(f"Not found '{SemDep.DEP_TYPE}' in dependence")
        self.type: str = str(data[SemDep.DEP_TYPE])
        self.is_closure: bool = bool(data[SemDep.IS_CLOSURE]) if SemDep.IS_CLOSURE in data else False
        self.excludes = None
        if SemDep.EXCLUDES in data:
            data_excludes = data[SemDep.EXCLUDES]
            if not isinstance(data_excludes, list):
                raise SemException(f"Field '{SemDep.EXCLUDES}' is not list")
            self.excludes: list[int] = [int(node_id) for node_id in data_excludes]

    def as_dict(self) -> dict:
        r = {
            SemGraph.DATATYPE: SemGraph.DATATYPE_DEP,
            SemDep.FROM_ID: self.from_id,
            SemDep.TO_ID: self.to_id,
            SemDep.DEP_TYPE: self.type,
        }
        if self.is_closure:
            r[SemDep.IS_CLOSURE] = True
        if self.excludes:
            r[SemDep.EXCLUDES] = self.excludes
        return r


class SemGraph:
    """Creating and reading sem-graph"""

    DATA = 'data'
    DATATYPE = 'DataType'
    DATATYPE_NODE = 'Node'
    DATATYPE_DEP = 'Dep'

    def __init__(self, config: SemConfig, skip_invalid: bool = False):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: SemConfig = config
        self.skip_invalid: bool = skip_invalid
        self.sem_graph_file: Path = None

    def make(self, **kwargs) -> None:
        """Make sem-graph file with current config params to ymake_root"""

        self.config.ymake_root.mkdir(0o755, parents=True, exist_ok=True)
        ymake_conf = self.config.ymake_root / 'ymake.conf'

        prepared_ymake_conf = kwargs.pop('prepared_ymake_conf', None)
        if prepared_ymake_conf:
            if prepared_ymake_conf != ymake_conf:
                shutil.copy(prepared_ymake_conf, ymake_conf)
        else:
            conf = build_facade.gen_conf(
                build_root=core_config.build_root(),
                build_type='nobuild',
                build_targets=self.config.params.abs_targets,
                flags=self.config.params.flags,
                host_platform=self.config.params.host_platform,
                target_platforms=self.config.params.target_platforms,
                arc_root=self.config.arcadia_root,
            )
            shutil.copy(conf, ymake_conf)

        dump_ymake_stderr = kwargs.pop('dump_ymake_stderr', None)
        for key, value in {  # Defaults from config, if absent in kwargs
            'source_root': self.config.arcadia_root,
            'custom_build_directory': self.config.ymake_root,
            'ymake_bin': self.config.ymake_bin,
        }.items():
            if key not in kwargs:
                kwargs[key] = value

        r, _ = ymake_sem_graph(
            custom_conf=ymake_conf,
            continue_on_fail=True,
            dump_sem_graph=True,
            abs_targets=self.config.params.abs_targets,
            **kwargs,
        )

        if dump_ymake_stderr:
            if dump_ymake_stderr == "log":
                self.logger.info("YMake call stderr:\n%s\n\n", r.stderr)
            else:
                try:
                    with open(dump_ymake_stderr, "w") as f:
                        f.write(r.stderr)
                    self.logger.info("YMake call stderr dumped to %s", dump_ymake_stderr)
                except Exception as e:
                    self.logger.error("Can't dump YMake call stderr to %s: %s", dump_ymake_stderr, e)
        if r.exit_code != 0:
            self.logger.error('Fail generate sem-graph by ymake with exit_code=%d:\n%s', r.exit_code, r.stderr)
            raise SemException(f'Fail generate sem-graph by ymake with exit_code={r.exit_code}')

        try:
            self.sem_graph_file: Path = self.config.ymake_root / 'sem.json'
            with self.sem_graph_file.open('w') as f:
                f.write(r.stdout)
        except Exception as e:
            raise SemException(f'Fail write sem-graph to {self.sem_graph_file}: {e}')

    @staticmethod
    def is_node(item: dict) -> bool:
        return item[SemGraph.DATATYPE] == SemGraph.DATATYPE_NODE

    @staticmethod
    def is_dep(item: dict) -> bool:
        return item[SemGraph.DATATYPE] == SemGraph.DATATYPE_DEP

    def read(self, all_nodes: bool = False) -> Iterable[SemNode | SemDep]:
        """Read sem-graph from file"""
        try:
            with self.sem_graph_file.open('rb') as f:
                graph = sjson.load(f)
            if SemGraph.DATA not in graph:
                raise SemException(f"Not found '{SemGraph.DATA}' in sem-graph")
            for data_item in graph[SemGraph.DATA]:
                try:
                    if SemGraph.DATATYPE not in data_item:
                        raise SemException(f"Not found '{SemGraph.DATATYPE}' in item {data_item}")
                    if self.is_node(data_item):
                        sem_node = SemNode(data_item, self.skip_invalid, self.logger if self.skip_invalid else None)
                        if not all_nodes and not sem_node.has_semantics():
                            continue  # node without semantics valid, but not interest
                        yield sem_node
                    elif self.is_dep(data_item):
                        yield SemDep(data_item)
                    else:
                        raise SemException(f"Unknown DataType '{data_item[SemGraph.DATATYPE]}'")
                except SemException as e:
                    item_type = 'item'
                    if self.is_node(data_item):
                        item_type = 'node'
                    elif self.is_dep(data_item):
                        item_type = 'dep'
                    if self.skip_invalid:
                        self.logger.warning("Skip invalid %s %s: %s", item_type, data_item, e)
                    else:
                        raise SemException(f"Fail parse {item_type} {data_item}: {e}") from e
        except Exception as e:
            raise SemException(f'Fail read sem-graph from {self.sem_graph_file}: {e}') from e

    def update(self, data: list[dict]) -> None:
        """Rewrite sem-graph file by updated data"""
        with self.sem_graph_file.open('wb') as f:  # Update sem-graph in file
            sjson.dump({SemGraph.DATA: data}, f)
