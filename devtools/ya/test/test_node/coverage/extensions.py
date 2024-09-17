from . import rigel

import devtools.ya.test.const


def inject_nlg_coverage_nodes(graph, suites, resolvers_map, opts, platform_descriptor):
    result = []
    for suite in [s for s in suites if s.get_ci_type_name() != "style"]:
        resolved_filename = devtools.ya.test.const.NLG_COVERAGE_RESOLVED_FILE_NAME
        uid = rigel.inject_unified_coverage_merger_node(graph, suite, resolved_filename, opts)
        result.append(uid)
        if resolvers_map is not None:
            resolvers_map[uid] = (resolved_filename, suite)
    return result
