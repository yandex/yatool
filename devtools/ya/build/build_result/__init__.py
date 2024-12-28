from collections import defaultdict

import enum
import dataclasses
import collections.abc

from devtools.ya.build.build_plan import BuildPlan
import devtools.ya.build.graph_description as graph_description
import devtools.ya.build.node_checks as node_checks

import typing as tp


type Errors = dict[graph_description.GraphNodeUid, list[str]]
type ErrorLinks = dict[graph_description.GraphNodeUid, list[str]]
type NodeByUid = dict[graph_description.GraphNodeUid, graph_description.GraphNode]
type NodeCache = defaultdict[graph_description.GraphNodeUid, set[graph_description.GraphNodeUid]]


class NodeStatus(enum.IntEnum):
    OK = 0
    BROKEN = 1
    BROKEN_BY_DEPS = 2


@dataclasses.dataclass
class _Caches:
    failed_deps_cache: NodeCache
    statuses_cache: dict[graph_description.GraphNodeUid, NodeStatus]
    build_errors_cache: defaultdict[graph_description.GraphNodeUid, set["BuildErrorWithLink"]]
    broken_deps_cache: NodeCache
    broken_nodes_cache: NodeCache


ERRORS_LIMIT = 30
BROKEN_DEPS_LIMIT = 20


class BuildErrorWithLink:
    def __init__(self, error, links):
        self.error = error
        self.links = links

    def __hash__(self):
        return self.error.__hash__()


def _limited_update[T: collections.abc.Hashable](dest: set[T], source: collections.abc.Iterable[T], limit: int) -> None:
    for x in source:
        if len(dest) >= limit:
            return
        dest.add(x)


def _calc_failed_deps(
    uid: graph_description.GraphNodeUid,
    errors: Errors,
    node_by_uid: NodeByUid,
    caches: _Caches,
) -> set[graph_description.GraphNodeUid]:
    failed_deps_cache = caches.failed_deps_cache
    if uid in errors:
        return {uid}
    if uid in failed_deps_cache:
        return failed_deps_cache[uid]
    for dep in node_by_uid[uid]['deps']:
        if _calc_failed_deps(dep, errors, node_by_uid, caches):
            _limited_update(failed_deps_cache[uid], [dep], BROKEN_DEPS_LIMIT)
    return failed_deps_cache[uid]


def _define_status(
    uid: graph_description.GraphNodeUid,
    errors: Errors,
    node_by_uid: NodeByUid,
    caches: _Caches,
) -> NodeStatus:
    statuses_cache = caches.statuses_cache
    if uid in statuses_cache:
        return statuses_cache[uid]
    if uid in errors:
        statuses_cache[uid] = NodeStatus.BROKEN
        return NodeStatus.BROKEN
    for dep in _calc_failed_deps(uid, errors, node_by_uid, caches):
        if (
            not node_checks.is_module(node_by_uid[dep])
            and _define_status(dep, errors, node_by_uid, caches) == NodeStatus.BROKEN
        ):
            statuses_cache[uid] = NodeStatus.BROKEN
            return NodeStatus.BROKEN
    statuses_cache[uid] = NodeStatus.BROKEN_BY_DEPS
    return NodeStatus.BROKEN_BY_DEPS


def _collect_build_errors(
    uid: graph_description.GraphNodeUid,
    errors: Errors,
    errors_links: ErrorLinks,
    node_by_uid: NodeByUid,
    caches: _Caches,
) -> tp.Iterable[BuildErrorWithLink]:
    build_errors_cache = caches.build_errors_cache
    if _define_status(uid, errors, node_by_uid, caches) == NodeStatus.BROKEN_BY_DEPS:
        return set()
    if uid in build_errors_cache:
        return build_errors_cache[uid]
    if uid in errors:
        build_errors_cache[uid].add(BuildErrorWithLink(errors[uid], errors_links.get(uid, [])))
    else:
        for dep in _calc_failed_deps(uid, errors, node_by_uid, caches):
            if (
                not node_checks.is_module(node_by_uid[dep])
                and _define_status(dep, errors, node_by_uid, caches) == NodeStatus.BROKEN
            ):
                _limited_update(
                    build_errors_cache[uid],
                    _collect_build_errors(dep, errors, errors_links, node_by_uid, caches),
                    ERRORS_LIMIT,
                )
    return build_errors_cache[uid]


def _collect_broken_project_deps(
    uid: graph_description.GraphNodeUid,
    errors: Errors,
    node_by_uid: NodeByUid,
    caches: _Caches,
) -> tp.Iterable[graph_description.GraphNodeUid]:
    broken_deps_cache = caches.broken_deps_cache
    if _define_status(uid, errors, node_by_uid, caches) == NodeStatus.BROKEN:
        return set()
    if uid in broken_deps_cache:
        return broken_deps_cache[uid]
    for dep in _calc_failed_deps(uid, errors, node_by_uid, caches):
        if node_checks.is_module(node_by_uid[dep]):
            _limited_update(broken_deps_cache[uid], {dep}, BROKEN_DEPS_LIMIT)
        else:
            _limited_update(
                broken_deps_cache[uid],
                _collect_broken_project_deps(dep, errors, node_by_uid, caches),
                BROKEN_DEPS_LIMIT,
            )
    return broken_deps_cache[uid]


def _collect_broken_nodes(
    uid: graph_description.GraphNodeUid,
    errors: Errors,
    node_by_uid: NodeByUid,
    caches: _Caches,
) -> tp.Iterable[graph_description.GraphNodeUid]:
    broken_nodes_cache = caches.broken_nodes_cache
    if uid in broken_nodes_cache:
        return broken_nodes_cache[uid]
    if uid in errors:
        broken_nodes_cache[uid].add(uid)
    else:
        for dep in _calc_failed_deps(uid, errors, node_by_uid, caches):
            _limited_update(
                broken_nodes_cache[uid], _collect_broken_nodes(dep, errors, node_by_uid, caches), ERRORS_LIMIT
            )
    return broken_nodes_cache[uid]


def make_build_errors_by_project(
    graph: list[graph_description.GraphNode],
    errors: Errors,
    errors_links: ErrorLinks,
):
    node_by_uid: NodeByUid = {node['uid']: node for node in graph}
    project_by_uid = {
        node['uid']: (BuildPlan.node_name(node), BuildPlan.node_platform(node), node['uid']) for node in graph
    }

    caches = _Caches(
        failed_deps_cache=defaultdict(set),
        statuses_cache=dict(),
        build_errors_cache=defaultdict(set),
        broken_deps_cache=defaultdict(set),
        broken_nodes_cache=defaultdict(set),
    )

    project_failed_deps = {}
    for uid, node in node_by_uid.items():
        if (
            node_checks.is_module(node)
            and _calc_failed_deps(uid, errors, node_by_uid, caches)
            and _define_status(uid, errors, node_by_uid, caches) == NodeStatus.BROKEN_BY_DEPS
        ):
            project_failed_deps[project_by_uid[uid]] = sorted(
                project_by_uid.get(x) for x in _collect_broken_project_deps(uid, errors, node_by_uid, caches)
            )

    project_build_errors_with_links = {}
    for project, deps in project_failed_deps.items():
        deps_paths = sorted(d[0] for d in deps)
        project_build_errors_with_links[project] = [
            BuildErrorWithLink('Depends on broken targets:\n{}'.format('\n'.join(deps_paths)), [])
        ]

    for uid, node in node_by_uid.items():
        if (
            node_checks.is_module(node)
            and _calc_failed_deps(uid, errors, node_by_uid, caches)
            and _define_status(uid, errors, node_by_uid, caches) == NodeStatus.BROKEN
        ):
            project_build_errors_with_links[project_by_uid[uid]] = sorted(
                _collect_build_errors(uid, errors, errors_links, node_by_uid, caches), key=lambda x: x.error
            )

    project_build_errors = {
        project: [e.error for e in errors] for project, errors in project_build_errors_with_links.items()
    }
    project_build_errors_links = {
        project: [e.links for e in errors] for project, errors in project_build_errors_with_links.items()
    }

    node_build_errors = {}
    node_build_errors_links = {}
    for uid in node_by_uid:
        node_errors = _collect_broken_nodes(uid, errors, node_by_uid, caches)
        if node_errors:
            node_build_errors[uid] = []
            node_build_errors_links[uid] = []
            for d in sorted(node_errors):
                node_build_errors[uid] += [(d, errors[d])]
                node_build_errors_links[uid] += [(d, errors_links.get(d, []))]

    return (
        project_build_errors,
        project_build_errors_links,
        project_failed_deps,
        node_build_errors,
        node_build_errors_links,
    )


class BuildResult:
    def __init__(
        self,
        errors,
        failed_deps,
        node_build_errors,
        ok_nodes=None,
        build_metrics=None,
        build_errors_links=None,
        node_build_errors_links=None,
        node_status_map=None,
        exit_code_map=None,
    ):
        self.build_errors = errors
        self.failed_deps = failed_deps
        self.node_build_errors = node_build_errors
        self.ok_nodes = ok_nodes or {}
        self.build_metrics = build_metrics or {}
        self.build_errors_links = build_errors_links or {}
        self.node_build_errors_links = node_build_errors_links or {}
        self.node_status_map = node_status_map or {}
        self.exit_code_map = exit_code_map or {}

    def get_full_result(self):
        merged = self.build_errors.copy()
        merged.update(self.failed_deps)
        return merged
