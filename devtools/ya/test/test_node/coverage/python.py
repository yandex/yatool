from . import rigel

import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import test.util.tools as util_tools
import test.const


def inject_python_coverage_nodes(graph, suites, resolvers_map, opts, platform_descriptor):
    py_suite_with_deps = []
    for suite in [s for s in suites if s.get_type() in ["pytest", "py3test"]]:
        py2, py3 = [], []
        for uid, output in rigel.get_suite_binary_deps(suite, graph):
            if graph.is_target_python2(uid):
                py2.append((uid, output))
            elif graph.is_target_python3(uid):
                py3.append((uid, output))
        if py2:
            py_suite_with_deps.append((suite, py2, 'py2'))
        if py3:
            py_suite_with_deps.append((suite, py3, 'py3'))
        assert py2 or py3, suite

    result = []

    if getattr(opts, 'build_coverage_report', False):
        py2s, py2d, py3s, py3d = [], [], [], []
        for suite, deps, prefix in py_suite_with_deps:
            if prefix == 'py2':
                py2s.append(suite)
                py2d.extend(deps)
            else:
                py3s.append(suite)
                py3d.extend(deps)

        for suites, py_bin_deps, prefix in [
            (py2s, py2d, 'py2'),
            (py3s, py3d, 'py3'),
        ]:
            if py_bin_deps:
                merged_coverage_filename = "$(BUILD_ROOT)/{}.coverage-merged.tar".format(prefix)
                merge_node_uid = inject_python_coverage_merge_node(
                    graph, suites, prefix, '{}.coverage.tar'.format(prefix), merged_coverage_filename, opts=opts
                )
                report_node_uid = inject_create_python_coverage_report_node(
                    graph, suites, py_bin_deps, merge_node_uid, prefix, merged_coverage_filename, opts
                )
                result.append(report_node_uid)

    for suite, py_bin_deps, prefix in py_suite_with_deps:
        if py_bin_deps:
            merged_coverage_filename = suite.work_dir('{}.coverage-merged.tar'.format(prefix))
            merge_node_uid = inject_python_coverage_merge_node(
                graph, [suite], prefix, '{}.coverage.tar'.format(prefix), merged_coverage_filename, opts=opts
            )

            resolved_filename = test.const.COVERAGE_RESOLVED_FILE_NAME_PATTERN.format(prefix)
            uid = inject_python_coverage_resolve_nodes(
                graph,
                suite,
                py_bin_deps,
                prefix,
                merged_coverage_filename,
                resolved_filename,
                merge_node_uid,
                opts=opts,
            )
            result.append(uid)

            if resolvers_map is not None:
                resolvers_map[uid] = (resolved_filename, suite)

    return result


def inject_python_coverage_merge_node(graph, suites, prefix, source_filename, output_path, opts=None):
    test_uids = uid_gen.get_test_result_uids(suites)
    uid = uid_gen.get_uid(test_uids + [output_path], "pycov-merge")
    all_resources = {}
    for suite in suites:
        all_resources.update(suite.global_resources)

    cmd = util_tools.get_test_tool_cmd(opts, "merge_python_coverage", all_resources) + [
        "--output",
        output_path,
        "--name-filter",
        ":{}:cov".format(prefix),
    ]

    coverage_paths = []
    for suite in suites:
        filename = suite.work_dir(source_filename)
        coverage_paths += ["--coverage-path", filename]

    cmd += coverage_paths

    node = {
        "cache": True,
        "broadcast": False,
        "inputs": [],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(test_uids),
        "env": {},
        "target_properties": {},
        "outputs": [output_path],
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


def inject_create_python_coverage_report_node(
    graph, suites, py_bin_deps, merge_node_uid, prefix, coverage_path, opts=None
):
    output_path = "$(BUILD_ROOT)/{}.coverage.report.tar".format(prefix)
    all_resources = {}
    for suite in suites:
        all_resources.update(suite.global_resources)

    cmd = util_tools.get_test_tool_cmd(opts, "build_python_coverage_report", all_resources, python=prefix) + [
        "--output",
        output_path,
        "--coverage-path",
        coverage_path,
        "--source-root",
        "$(SOURCE_ROOT)",
        # "--verbose",
    ]

    deps, binaries = zip(*py_bin_deps)
    deps = list(deps) + [merge_node_uid]

    for binary in binaries:
        cmd += ["--binary", binary]

    uid = uid_gen.get_uid(deps, "pycov-report")

    node = {
        "broadcast": False,
        "inputs": [],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": deps,
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
                'cmd_args': cmd,
                'cwd': '$(BUILD_ROOT)',
            }
        ],
    }
    graph.append_node(node, add_to_result=True)

    return uid


def inject_python_coverage_resolve_nodes(
    graph, suite, py_bin_deps, prefix, coverage_tar_path, resolved_filename, merge_node_uid, opts=None
):
    work_dir = suite.work_dir()
    output_path = "{}/{}".format(work_dir, resolved_filename)
    log_path = "{}/{}_coverage_resolve.log".format(work_dir, prefix)

    deps, binaries = zip(*py_bin_deps)
    deps = list(deps) + [merge_node_uid]

    cmd = util_tools.get_test_tool_cmd(opts, "resolve_python_coverage", suite.global_resources, python=prefix) + [
        "--coverage-path",
        coverage_tar_path,
        "--source-root",
        "$(SOURCE_ROOT)",
        "--output",
        output_path,
        "--log-path",
        log_path,
    ]
    for binary in binaries:
        cmd += ["--binary", binary]

    uid = uid_gen.get_uid(deps, "resolve_pycov")

    node = {
        "cache": True,
        "broadcast": False,
        "inputs": [coverage_tar_path],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(deps),
        "env": {},
        "target_properties": {},
        "outputs": [output_path, log_path],
        'kv': {
            # Resolve Python coverage
            "p": "RP",
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
