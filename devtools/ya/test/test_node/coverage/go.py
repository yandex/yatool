import os

import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import devtools.ya.test.const
import devtools.ya.test.util.tools as util_tools


def inject_go_coverage_nodes(graph, suites, resolvers_map, opts, platform_descriptor):
    result = []
    gocov_filename = 'go.coverage.tar'

    if getattr(opts, 'build_coverage_report', False):
        report_node_uid = inject_create_go_coverage_report_node(graph, suites, gocov_filename, opts)
        result.append(report_node_uid)

    for suite in [s for s in suites if s.get_type() == "go_test"]:
        resolved_filename = devtools.ya.test.const.GO_COVERAGE_RESOLVED_FILE_NAME
        uid = inject_go_coverage_resolve_nodes(graph, suite, gocov_filename, resolved_filename, opts=opts)
        result.append(uid)

        if resolvers_map is not None:
            resolvers_map[uid] = (resolved_filename, suite)

    return result


def inject_create_go_coverage_report_node(graph, suites, coverage_path, opts):
    output_path = os.path.join("$(BUILD_ROOT)", "go.coverage.report.tar")
    all_resources = {}
    go_path = str()
    for suite in suites:
        all_resources.update(suite.global_resources)
        go_path = suite.global_resources.get(devtools.ya.test.const.GO_TOOLS_RESOURCE)
    cmd = util_tools.get_test_tool_cmd(opts, "build_go_coverage_report", all_resources) + [
        "--output",
        output_path,
        "--gotools-path",
        go_path,
        "--source-root",
        "$(SOURCE_ROOT)",
        "--verbose",
    ]
    if opts.coverage_prefix_filter:
        cmd.extend(['--prefix-filter', opts.coverage_prefix_filter])
    if opts.coverage_exclude_regexp:
        cmd.extend(['--exclude-regexp', opts.coverage_exclude_regexp])

    for suite in [s for s in suites if s.get_type() == "go_test"]:
        coverage_tar_path = suite.work_dir(coverage_path)
        cmd += ["--coverage-tars", coverage_tar_path]
    deps = uid_gen.get_test_result_uids(suites)
    uid = uid_gen.get_uid(deps, "gocov-report")

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": [],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(deps),
        "env": {"GOROOT": go_path},
        "target_properties": {},
        "outputs": [output_path],
        "tared_outputs": [output_path],
        'kv': {
            "p": "CV",
            "pc": 'light-cyan',
            "show_out": True,
        },
        "cmds": [
            {
                'cmd_args': cmd,
                'cwd': '$(BUILD_ROOT)',
            }
        ],
    }
    graph.append_node(node, add_to_result=True)

    return uid


def inject_go_coverage_resolve_nodes(graph, suite, coverage_tar_path, resolved_filename, opts=None):
    test_uid = uid_gen.get_test_result_uids([suite])
    coverage_tar_path = suite.work_dir(coverage_tar_path)
    uid = uid_gen.get_uid(test_uid, "resolve_gocov")
    output_path = suite.work_dir(resolved_filename)
    log_path = suite.work_dir("go_coverage_resolve.log")

    cmd = util_tools.get_test_tool_cmd(opts, "resolve_go_coverage", suite.global_resources) + [
        "--coverage-path",
        coverage_tar_path,
        "--output",
        output_path,
        "--log-path",
        log_path,
    ]

    if opts.coverage_prefix_filter:
        cmd.extend(['--prefix-filter', opts.coverage_prefix_filter])
    if opts.coverage_exclude_regexp:
        cmd.extend(['--exclude-regexp', opts.coverage_exclude_regexp])

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": True,
        "broadcast": False,
        "inputs": [coverage_tar_path],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(test_uid),
        "env": {},
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "outputs": [output_path, log_path],
        'kv': {
            # Resolve Go
            "p": "RG",
            "pc": 'cyan',
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
