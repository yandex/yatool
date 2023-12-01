from collections import defaultdict

import six

from build.build_plan import BuildPlan

OK, BROKEN, BROKEN_BY_DEPS = range(3)

ERRORS_LIMIT = 30
BROKEN_DEPS_LIMIT = 20


class BuildErrorWithLink(object):
    def __init__(self, error, links):
        self.error = error
        self.links = links

    def __hash__(self):
        return self.error.__hash__()


def make_build_errors_by_project(graph, errors, errors_links):
    node_by_uid = {node['uid']: node for node in graph}
    project_by_uid = {
        node['uid']: (BuildPlan.node_name(node), BuildPlan.node_platform(node), node['uid']) for node in graph
    }

    def limited_union(dest, source, limit):
        for x in source:
            if len(dest) >= limit:
                return
            dest.add(x)

    def is_module(uid):
        target_properties = node_by_uid[uid].get('target_properties', {})
        return 'module_type' in target_properties or target_properties.get('is_module', False)

    failed_deps_cache = defaultdict(set)

    def calc_failed_deps(uid):
        if uid in errors:
            return [uid]
        if uid in failed_deps_cache:
            return failed_deps_cache[uid]
        for dep in node_by_uid[uid]['deps']:
            if calc_failed_deps(dep):
                limited_union(failed_deps_cache[uid], [dep], BROKEN_DEPS_LIMIT)
        return failed_deps_cache[uid]

    statuses_cache = {}

    def define_status(uid):
        if uid in statuses_cache:
            return statuses_cache[uid]
        if uid in errors:
            statuses_cache[uid] = BROKEN
            return BROKEN
        for dep in calc_failed_deps(uid):
            if not is_module(dep) and define_status(dep) == BROKEN:
                statuses_cache[uid] = BROKEN
                return BROKEN
        statuses_cache[uid] = BROKEN_BY_DEPS
        return BROKEN_BY_DEPS

    build_errors_cache = defaultdict(set)

    def collect_build_errors(uid):
        if define_status(uid) == BROKEN_BY_DEPS:
            return set()
        if uid in build_errors_cache:
            return build_errors_cache[uid]
        if uid in errors:
            build_errors_cache[uid].add(BuildErrorWithLink(errors[uid], errors_links.get(uid, [])))
        else:
            for dep in calc_failed_deps(uid):
                if not is_module(dep) and define_status(dep) == BROKEN:
                    limited_union(build_errors_cache[uid], collect_build_errors(dep), ERRORS_LIMIT)
        return build_errors_cache[uid]

    broken_deps_cache = defaultdict(set)

    def collect_broken_project_deps(uid):
        if define_status(uid) == BROKEN:
            return set()
        if uid in broken_deps_cache:
            return broken_deps_cache[uid]
        for dep in calc_failed_deps(uid):
            if is_module(dep):
                limited_union(broken_deps_cache[uid], {dep}, BROKEN_DEPS_LIMIT)
            else:
                limited_union(broken_deps_cache[uid], collect_broken_project_deps(dep), BROKEN_DEPS_LIMIT)
        return broken_deps_cache[uid]

    broken_nodes_cache = defaultdict(set)

    def collect_broken_nodes(uid):
        if uid in broken_nodes_cache:
            return broken_nodes_cache[uid]
        if uid in errors:
            broken_nodes_cache[uid].add(uid)
        else:
            for dep in calc_failed_deps(uid):
                limited_union(broken_nodes_cache[uid], collect_broken_nodes(dep), ERRORS_LIMIT)
        return broken_nodes_cache[uid]

    project_failed_deps = {}
    for uid in six.iterkeys(node_by_uid):
        if is_module(uid) and calc_failed_deps(uid) and define_status(uid) == BROKEN_BY_DEPS:
            project_failed_deps[project_by_uid[uid]] = sorted(map(project_by_uid.get, collect_broken_project_deps(uid)))

    project_build_errors_with_links = {}
    for project, deps in six.iteritems(project_failed_deps):
        deps_paths = sorted([d[0] for d in deps])
        project_build_errors_with_links[project] = [
            BuildErrorWithLink('Depends on broken targets:\n{}'.format('\n'.join(deps_paths)), [])
        ]

    for uid in six.iterkeys(node_by_uid):
        if is_module(uid) and calc_failed_deps(uid) and define_status(uid) == BROKEN:
            project_build_errors_with_links[project_by_uid[uid]] = sorted(
                collect_build_errors(uid), key=lambda x: x.error
            )

    project_build_errors = {
        project: [e.error for e in errors] for project, errors in six.iteritems(project_build_errors_with_links)
    }
    project_build_errors_links = {
        project: [e.links for e in errors] for project, errors in six.iteritems(project_build_errors_with_links)
    }

    node_build_errors = {}
    node_build_errors_links = {}
    for uid in six.iterkeys(node_by_uid):
        node_errors = collect_broken_nodes(uid)
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


class BuildResult(object):
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
