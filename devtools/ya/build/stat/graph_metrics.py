# cython: profile=True

import typing
import logging
from collections import defaultdict

from devtools.ya.build.node_checks import is_module, is_binary
from devtools.ya.build.graph_description import GraphNodeUid, GraphNode

logger = logging.getLogger(__name__)


def _collect_module_deps_stats(
    start_uid: GraphNodeUid,
    nodes: dict[GraphNodeUid, GraphNode],
    module_uids: set[GraphNodeUid],
    tasks_metrics: dict[str, dict],
) -> tuple[int, int | float | None]:
    """Walk non-module deps of the module stopping at other modules.

    Returns the count of distinct modules reached and the total elapsed time
    of the module itself plus all visited non-module nodes
    (None if elapsed is unknown for any of them).

    Iterative to survive arbitrarily deep dependency chains without hitting the recursion limit.
    """
    module_count = 0
    elapsed = tasks_metrics.get(start_uid, {}).get('elapsed')

    visited = {start_uid}
    stack = [start_uid]
    while stack:
        v = stack.pop()
        node = nodes.get(v)
        if node is None:
            continue
        for d in node['deps']:
            if d == start_uid:
                logger.warning("Detect circular dependency for `%s` in deps for `%s`", d, v)
                logger.debug("Node: %s", node)
                continue
            if d in visited:
                continue
            visited.add(d)
            if d in module_uids:
                module_count += 1
                continue
            if elapsed is not None:
                dep_elapsed = tasks_metrics.get(d, {}).get('elapsed')
                elapsed = None if dep_elapsed is None else elapsed + dep_elapsed
            stack.append(d)

    return module_count, elapsed


def _add_metric(
    n: GraphNode, name: str, value: typing.Any, metrics: typing.DefaultDict[GraphNodeUid, typing.Any]
) -> None:
    metrics[n['uid']].update({name: value})


def make_targets_metrics(
    graph: typing.List[GraphNode], tasks_metrics: typing.Dict[str, dict], execution_log: dict[str, dict]
) -> typing.DefaultDict:
    metrics = defaultdict(dict)

    nodes: dict[GraphNodeUid, GraphNode] = {}
    module_uids: set[GraphNodeUid] = set()
    for node in graph:
        nodes[node['uid']] = node
        if is_module(node):
            module_uids.add(node['uid'])

    for node in graph:
        uid = node['uid']
        if uid not in module_uids:
            continue

        task_metrics = tasks_metrics.get(uid, {})
        if 'size' in task_metrics:
            size = task_metrics['size']
        else:
            # Constructing a key that matches string representation of a dist download task: devtools/ya/yalibrary/runner/tasks/distbuild/__init__.py
            key = f'DistDownload({uid})'
            size = execution_log[key]['size'] if key in execution_log else -1

        if size != -1:
            _add_metric(node, 'artifacts-size', size, metrics)

        module_count, elapsed = _collect_module_deps_stats(uid, nodes, module_uids, tasks_metrics)
        if is_binary(node):
            _add_metric(node, 'dependencies-count', module_count, metrics)

        if elapsed is not None:
            _add_metric(node, 'build-time', elapsed, metrics)

    return metrics
