# cython: profile=True

import logging
from collections import defaultdict
import six

from build.node_checks import is_module, is_binary


try:
    RecursionError
except NameError:
    # python3.4
    RecursionError = RuntimeError

logger = logging.getLogger(__name__)


def make_dependencies_lists(graph):
    nodes = {}
    deps = defaultdict(set)
    for node in graph:
        uid = node['uid']
        nodes[uid] = node
        for dep in node['deps']:
            deps[uid].add(dep)

    def traverse(start_node, v, modules, not_modules):
        if v in modules or v in not_modules:
            return

        if is_module(nodes[v]) and v != start_node:
            modules.add(v)
        else:
            if v != start_node:
                not_modules.add(v)
            for d in deps[v]:
                if d == start_node:
                    logger.warning("Detect circular dependency for `%s` in deps for `%s`", d, v)
                    logger.debug("Node: %s", nodes[v])
                else:
                    traverse(start_node, d, modules, not_modules)

    result = {}
    for uid, node in six.iteritems(nodes):
        if is_module(node):
            modules = set()
            not_modules = set()
            try:
                traverse(uid, uid, modules, not_modules)
            except RecursionError:
                logger.exception("While traversing uid `%s`", uid)
                logger.debug("Node: %s", nodes[uid])
                logger.debug("Deps for uid `%s`: %s", uid, deps[uid])
                raise
            result[uid] = (modules, not_modules)
    return result


def make_targets_metrics(graph, tasks_metrics):
    metrics = defaultdict(dict)
    deps = make_dependencies_lists(graph)

    def add(n, name, value):
        metrics[n['uid']].update({name: value})

    def calculate_elapsed(deps):
        res = 0
        for dep in deps:
            if tasks_metrics.get(dep, {}).get('elapsed') is None:
                return None
            res += tasks_metrics[dep]['elapsed']
        return res

    for node in graph:
        if is_module(node):
            uid = node['uid']
            task_metrics = tasks_metrics.get(uid, {})
            if 'size' in task_metrics:
                add(node, 'artifacts-size', task_metrics['size'])

            if uid in deps:
                modules, not_modules = deps[uid]
                if is_binary(node):
                    add(node, 'dependencies-count', len(modules))

                elapsed = calculate_elapsed(list(not_modules) + [uid])
                if elapsed is not None:
                    add(node, 'build-time', elapsed)

    return metrics
