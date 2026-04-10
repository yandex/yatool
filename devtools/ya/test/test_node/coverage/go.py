from pathlib import Path

import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import devtools.ya.test.const
import devtools.ya.test.util.tools as util_tools


def inject_go_coverage_nodes(graph, suites, resolvers_map, opts, platform_descriptor):
    result = []
    coverage_tar_fn = 'go.coverage.tar'
    merged_coverage_tar_fn = 'go.merged-coverage.tar'

    if getattr(opts, 'build_coverage_report', False):
        if opts.go_coverage_per_pkg:
            merged_coverage_tar = str(Path("$(BUILD_ROOT)") / merged_coverage_tar_fn)
            merge_node_uid = inject_go_coverage_merge_node(
                graph, suites, coverage_tar_fn, merged_coverage_tar, '', opts
            )
            report_node_uid = inject_create_go_coverage_report_node(
                graph, suites, merge_node_uid, merged_coverage_tar, '', opts
            )
        else:
            report_node_uid = inject_create_go_coverage_report_node(graph, suites, None, None, coverage_tar_fn, opts)
        result.append(report_node_uid)

    resolved_fn = devtools.ya.test.const.GO_COVERAGE_RESOLVED_FILE_NAME
    for suite in [s for s in suites if s.get_type() == "go_test"]:
        if opts.go_coverage_per_pkg:
            merged_coverage_tar = suite.work_dir(merged_coverage_tar_fn)
            merge_node_uid = inject_go_coverage_merge_node(
                graph, [suite], coverage_tar_fn, merged_coverage_tar, 'a.yandex-team.ru/', opts
            )
            uid = inject_go_coverage_resolve_nodes(graph, suite, merge_node_uid, merged_coverage_tar, resolved_fn, opts)
        else:
            uid = inject_go_coverage_resolve_nodes(
                graph, suite, None, suite.work_dir(coverage_tar_fn), resolved_fn, opts
            )
        result.append(uid)

        if resolvers_map is not None:
            resolvers_map[uid] = (resolved_fn, suite)

    return result


def inject_go_coverage_merge_node(graph, suites, coverage_tar_fn, output_path, filename_prefix, opts):
    test_uids = uid_gen.get_test_result_uids(suites)
    uid = uid_gen.get_uid(test_uids + [output_path], "gocov-merge")
    all_resources = {}
    go_path = str()
    coverage_paths = []
    for suite in suites:
        all_resources.update(suite.global_resources)
        go_path = suite.global_resources.get(devtools.ya.test.const.GO_TOOLS_RESOURCE)
        coverage_paths.append(suite.work_dir(coverage_tar_fn))

    cmd = (
        util_tools.get_test_tool_cmd(opts, "merge_go_coverage", all_resources)
        + [
            "--output",
            output_path,
            "--gotools-path",
            go_path,
            "--source-root",
            "$(SOURCE_ROOT)",
        ]
        + (["--filename-prefix", filename_prefix] if filename_prefix else [])
    )
    for coverage_path in coverage_paths:
        cmd.extend(['--coverage-path', coverage_path])

    if opts.coverage_prefix_filter:
        cmd.extend(['--prefix-filter', opts.coverage_prefix_filter])
    if opts.coverage_exclude_regexp:
        cmd.extend(['--exclude-regexp', opts.coverage_exclude_regexp])

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": True,
        "broadcast": False,
        "inputs": coverage_paths,
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(test_uids),
        "env": {},
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
                "cmd_args": cmd,
                "cwd": "$(BUILD_ROOT)",
            },
        ],
    }
    graph.append_node(node, add_to_result=False)

    return uid


def inject_create_go_coverage_report_node(graph, suites, merge_node_uid, merged_coverage_tar, coverage_tar_fn, opts):
    output_path = str(Path("$(BUILD_ROOT)") / "go.coverage.report.tar")
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

    coverage_tars = []
    deps = uid_gen.get_test_result_uids(suites)
    if opts.go_coverage_per_pkg:
        cmd.extend(["--merged-coverage-tar", merged_coverage_tar])
        coverage_tars.append(merged_coverage_tar)
        deps += [merge_node_uid]
    else:
        if opts.coverage_prefix_filter:
            cmd.extend(['--prefix-filter', opts.coverage_prefix_filter])
        if opts.coverage_exclude_regexp:
            cmd.extend(['--exclude-regexp', opts.coverage_exclude_regexp])
        for suite in [s for s in suites if s.get_type() == "go_test"]:
            suite_coverage_tar = suite.work_dir(coverage_tar_fn)
            cmd += ["--coverage-tars", suite_coverage_tar]
            coverage_tars.append(suite_coverage_tar)

    uid = uid_gen.get_uid(deps, "gocov-report")

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": coverage_tars,
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


def inject_go_coverage_resolve_nodes(graph, suite, merge_node_uid, coverage_tar, resolved_fn, opts):
    test_uids = uid_gen.get_test_result_uids([suite]) + ([merge_node_uid] if merge_node_uid else [])
    uid = uid_gen.get_uid(test_uids, "gocov-resolve")
    output_path = suite.work_dir(resolved_fn)
    log_path = suite.work_dir("go_coverage_resolve.log")

    cmd = util_tools.get_test_tool_cmd(opts, "resolve_go_coverage", suite.global_resources) + [
        "--output",
        output_path,
        "--log-path",
        log_path,
    ]

    if opts.go_coverage_per_pkg:
        cmd.extend(["--merged-coverage-tar", coverage_tar])
    else:
        cmd.extend(["--coverage-path", coverage_tar])
        if opts.coverage_prefix_filter:
            cmd.extend(['--prefix-filter', opts.coverage_prefix_filter])
        if opts.coverage_exclude_regexp:
            cmd.extend(['--exclude-regexp', opts.coverage_exclude_regexp])

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": True,
        "broadcast": False,
        "inputs": [coverage_tar],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(test_uids),
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
