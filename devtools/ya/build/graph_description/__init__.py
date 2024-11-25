from typing import TypedDict, Union, Any, NewType, NotRequired


class GraphConfResourceConcreteInfo(TypedDict):
    pattern: str
    resource: str


class ResourcePlatformInfo(TypedDict):
    platform: str
    resource: str


class GraphConfResourceByPlatformInfo(TypedDict):
    pattern: str
    resources: list[ResourcePlatformInfo]


GraphConfResourceInfo = Union[GraphConfResourceConcreteInfo, GraphConfResourceByPlatformInfo]


class GraphConfSection(TypedDict):
    cache: bool
    default_node_requirements: dict
    description: dict
    execution_cost: dict
    graph_size: int
    gsid: str
    keepon: bool
    min_reqs_errors: int
    platform: str
    resources: list[GraphConfResourceInfo]


GraphNodeUid = NewType('GraphNodeUid', str)


class GraphNodeTargetProperties(TypedDict):
    # TODO: add all possible target_properties
    module_type: NotRequired[str]
    module_lang: NotRequired[str]
    module_dir: NotRequired[str]
    is_module: NotRequired[bool]
    run: NotRequired[bool]


GraphNode = TypedDict(
    'GraphNode',
    {
        'cmds': list[Any],
        'cwd': str,
        'env': dict[str, str],
        'inputs': list[str],
        'kv': dict[str, str],
        'node-type': str,
        'platform': str,
        'uid': GraphNodeUid,
        'deps': list[GraphNodeUid],
        'outputs': list[str],
        'timeout': int,
        'cache': bool,
        'type': int,
        'target_properties': NotRequired[GraphNodeTargetProperties],
    },
)


GraphResult = list[GraphNodeUid]


class DictGraph(TypedDict):
    conf: GraphConfSection
    graph: list[GraphNode]
    inputs: dict[str, Any]
    result: GraphResult
