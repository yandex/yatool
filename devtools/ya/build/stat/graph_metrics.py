# cython: profile=True

import typing
import logging
from collections import defaultdict

from devtools.ya.build.node_checks import is_module, is_binary
from devtools.ya.build.graph_description import GraphNodeUid, GraphNode


logger = logging.getLogger(__name__)


def _traverse_deps(
    start_node: GraphNodeUid,
    v: GraphNodeUid,
    modules: typing.Set[GraphNodeUid],
    not_modules: typing.Set[GraphNodeUid],
    nodes: typing.Dict[GraphNodeUid, GraphNode],
    deps: typing.DefaultDict[GraphNodeUid, typing.List[GraphNodeUid]],
) -> None:
    if v in modules or v in not_modules:
        return

    if is_module(nodes[v]) and v != start_node:
        modules.add(v)
    else:
        if v != start_node:
            not_modules.add(v)
        if v not in deps:
            return
        for d in deps[v]:
            if d == start_node:
                logger.warning("Detect circular dependency for `%s` in deps for `%s`", d, v)
                logger.debug("Node: %s", nodes[v])
            else:
                _traverse_deps(start_node, d, modules, not_modules, nodes, deps)


def _make_dependencies_lists(graph: typing.List[GraphNode]) -> typing.Dict:
    nodes = {}
    deps = defaultdict(list)
    for node in graph:
        uid = node['uid']
        nodes[uid] = node
        for dep in set(node['deps']):
            deps[uid].append(dep)

    result = {}
    for uid, node in nodes.items():
        if is_module(node):
            modules = set()
            not_modules = set()
            try:
                _traverse_deps(uid, uid, modules, not_modules, nodes, deps)
            except RecursionError:
                logger.exception("While traversing uid `%s`", uid)
                logger.debug("Node: %s", node)
                logger.debug("Deps for uid `%s`: %s", uid, deps[uid])
                raise
            result[uid] = (len(modules), not_modules)
    return result


def _calculate_elapsed_time_by_deps(
    deps: typing.List[GraphNodeUid], tasks_metrics: typing.Dict
) -> typing.Union[int, float]:
    res = 0
    for dep in deps:
        if tasks_metrics.get(dep, {}).get('elapsed') is None:
            return None
        res += tasks_metrics[dep]['elapsed']
    return res


def _add_metric(
    n: GraphNode, name: str, value: typing.Any, metrics: typing.DefaultDict[GraphNodeUid, typing.Any]
) -> None:
    metrics[n['uid']].update({name: value})


def make_targets_metrics(
    graph: typing.List[GraphNode], tasks_metrics: typing.Dict[str, dict], execution_log: dict[str, dict]
) -> typing.DefaultDict:
    metrics = defaultdict(dict)
    deps = _make_dependencies_lists(graph)

    for node in graph:
        if not is_module(node):
            continue

        uid = node['uid']
        task_metrics = tasks_metrics.get(uid, {})
        if 'size' in task_metrics:
            size = task_metrics['size']
        else:
            # Constructing a key that matches string representation of a dist download task: devtools/ya/yalibrary/runner/tasks/distbuild/__init__.py
            key = f'DistDownload({uid})'
            size = execution_log[key]['size'] if key in execution_log else -1

        if size != -1:
            _add_metric(node, 'artifacts-size', size, metrics)

        if uid in deps:
            module_count, not_modules = deps[uid]
            if is_binary(node):
                _add_metric(node, 'dependencies-count', module_count, metrics)

            elapsed = _calculate_elapsed_time_by_deps(list(not_modules) + [uid], tasks_metrics)
            if elapsed is not None:
                _add_metric(node, 'build-time', elapsed, metrics)

    return metrics
