import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import logging
import test.const
import test.util.tools as util_tools

logger = logging.getLogger(__name__)

TS_PREFIX = "ts"
COVERAGE_TAR_RESULT = "ts.coverage.tar"


def inject_ts_coverage_nodes(graph, suites, resolvers_map, opts, platform_descriptor):
    added_uids = []

    # For each suite we are to collect coverage data info, so injecting a coverage_resolve nodes
    # Jest suite nodes will produce initial coverage info files in jest format
    # and then in the coverage_resolve nodes we will produce the required data structure based on that
    jest_suites = [s for s in suites if s.get_type() == "jest"]
    if jest_suites:
        for suite in jest_suites:
            suite_res_cov_filename = suite.work_dir(COVERAGE_TAR_RESULT)
            cov_resolved_filename = test.const.TS_COVERAGE_RESOLVED_FILE_NAME
            cov_resolve_uid = inject_ts_coverage_resolve_node(
                graph,
                suite,
                suite_res_cov_filename,
                cov_resolved_filename,
                opts=opts,
            )
            added_uids.append(cov_resolve_uid)

            if resolvers_map is not None:
                resolvers_map[cov_resolve_uid] = (cov_resolved_filename, suite)

        if getattr(opts, "build_coverage_report", False):
            report_node_uid = inject_create_ts_coverage_report_node(graph, jest_suites, added_uids, opts)
            added_uids.append(report_node_uid)

    return added_uids


def inject_create_ts_coverage_report_node(graph, suites, resolve_uids, opts=None):
    if not suites:
        return None

    output_path = "$(BUILD_ROOT)/ts.coverage.report.tar"
    log_path = "$(BUILD_ROOT)/ts_coverage_report.log"
    all_resources = {}
    for suite in suites:
        all_resources.update(suite.global_resources)

    nodejs_resource = suite.dart_info.get(suite.dart_info.get("NODEJS-ROOT-VAR-NAME"))
    nyc_resource = suite.dart_info.get(suite.dart_info.get("NYC-ROOT-VAR-NAME"))

    cmd = util_tools.get_test_tool_cmd(opts, "build_ts_coverage_report", all_resources) + [
        "--output",
        output_path,
        "--source-root",
        "$(SOURCE_ROOT)",
        "--nodejs-resource",
        nodejs_resource,
        "--nyc-resource",
        nyc_resource,
        "--log-path",
        log_path,
    ]
    inputs_for_node = []
    for suite in suites:
        coverage_tar_path = suite.work_dir(COVERAGE_TAR_RESULT)
        cmd += ["--coverage-tars", coverage_tar_path]
        inputs_for_node.append(coverage_tar_path)

    deps = uid_gen.get_test_result_uids(suites)
    uid = uid_gen.get_uid(deps, "tscov-report")

    node = {
        "cache": False,
        "broadcast": False,
        "inputs": inputs_for_node,
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(resolve_uids),
        "env": {},
        "target_properties": {},
        "outputs": [output_path, log_path],
        "tared_outputs": [output_path],
        "kv": {
            "p": "CV",
            "pc": "light-cyan",
            "show_out": True,
        },
        "cmds": [
            {
                "cmd_args": cmd,
                "cwd": "$(BUILD_ROOT)",
            }
        ],
    }
    graph.append_node(node, add_to_result=True)

    return uid


def inject_ts_coverage_resolve_node(graph, suite, coverage_tar_path, resolved_filename, opts=None):
    output_path = suite.work_dir(resolved_filename)
    log_path = suite.work_dir("ts_coverage_resolve.log")

    cmd = util_tools.get_test_tool_cmd(opts, "resolve_ts_coverage", suite.global_resources) + [
        "--project-path",
        suite.dart_info.get("TS-TEST-FOR-PATH"),
        "--coverage-path",
        coverage_tar_path,
        "--output",
        output_path,
        "--log-path",
        log_path,
    ]

    deps = uid_gen.get_test_result_uids([suite])
    uid = uid_gen.get_uid(deps, "resolve_tscov")

    node = {
        "cache": False,
        "broadcast": False,
        "inputs": [coverage_tar_path],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(deps),
        "env": {},
        "target_properties": {},
        # Keep coverage_tar_path as could use it when building overall html report
        "outputs": [coverage_tar_path, output_path, log_path],
        "kv": {
            # Resolve TypeScript coverage
            "p": "RT",
            "pc": "cyan",
            "show_out": True,
        },
        "cmds": [
            {
                "cmd_args": cmd,
                "cwd": "$(BUILD_ROOT)",
            },
        ],
    }
    graph.append_node(node, add_to_result=True)
    return uid
