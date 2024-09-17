import os
import logging

import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import devtools.ya.test.common as test_common
import devtools.ya.test.const
import devtools.ya.test.util.tools as util_tools

logger = logging.getLogger(__name__)


def get_suite_binary_deps(suite, graph, skip_module_lang=None):
    res = set()
    seen = set()
    seen_outputs = {}
    for dep in suite.get_build_dep_uids():
        for uid, outputs in graph.get_target_outputs_by_type(dep, ['bin', 'so'], unroll_bundle=True, kvp=["BN"]):
            if uid in seen:
                continue
            seen.add(uid)

            node = graph.get_node_by_uid(uid)

            if skip_module_lang and node.get('target_properties', {}).get('module_lang') in skip_module_lang:
                continue

            # XXX This is a workaround for targets that produce lots of outputs in a link node.
            # There is only one binary and it's the first one in the output list (this case has a test)
            if node.get('kv').get('p') == 'LD':
                outputs = [outputs[0]]

            for output in outputs:
                if os.path.splitext(output)[1] in devtools.ya.test.const.FAKE_OUTPUT_EXTS:
                    continue

                # verify there is only one output
                if output in seen_outputs:
                    logger.warning(
                        "Skipping already discovered output: %s (suite:%s uid1:%s uid2:%s)",
                        output,
                        suite,
                        uid,
                        seen_outputs[output],
                    )
                    continue

                seen_outputs[output] = uid
                # Add direct dependency, not suite's one,
                # because suite's dependency might be package (bundle),
                # witch lays out output data differently,
                # and output will not point to the actual location
                res.add((uid, output))
    return res


def inject_coverage_merge_node(graph, tests, source_filename, result_filename, opts=None):
    test_uids = uid_gen.get_test_result_uids(tests)
    cache_node = True

    uid = uid_gen.get_random_uid("merge-coverage")

    output_path = os.path.join("$(BUILD_ROOT)", result_filename)
    merge_coverage_script = os.path.join("$(SOURCE_ROOT)", 'build', 'scripts', 'merge_coverage_data.py')

    result_cmd = [merge_coverage_script, output_path]

    for suite in tests:
        result_cmd.append(suite.work_dir(source_filename))
    result_cmd += ['-no-merge', 'report.exec']

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": cache_node,
        "broadcast": False,
        "inputs": [merge_coverage_script],
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
                "cmd_args": test_common.get_python_cmd(opts=opts) + result_cmd,
                "cwd": "$(BUILD_ROOT)",
            },
        ],
    }
    graph.append_node(node, add_to_result=False)

    return uid


def inject_unified_coverage_merger_node(graph, suite, resolved_filename, opts):
    test_uid = uid_gen.get_test_result_uids([suite])
    work_dir = suite.work_dir()
    output_path = os.path.join(work_dir, resolved_filename)
    resolved_coverage_paths = [work_dir]
    uid = uid_gen.get_uid(test_uid, "coverage_unified_merger")
    cmd = util_tools.get_test_tool_cmd(opts, "merge_coverage_inplace", suite.global_resources) + [
        "--output",
        output_path,
    ]
    for cov_path in resolved_coverage_paths:
        cmd += ["--coverage-paths", cov_path]
    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": True,
        "broadcast": False,
        "inputs": resolved_coverage_paths,
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(test_uid),
        "env": {},
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "outputs": [output_path],
        'kv': {
            "p": "RC",
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


def inject_inplace_coverage_merger_node(graph, coverage_uids, suites, opts):
    resolved_coverage_paths = []
    for suite in suites:
        work_dir = suite.work_dir()
        suite_wd_has_resolved_coverage = False
        if suite.get_type() == 'go_test' and getattr(opts, "go_coverage", False):
            suite_wd_has_resolved_coverage = True
        elif suite.get_type() == 'jest' and getattr(opts, "ts_coverage", False):
            suite_wd_has_resolved_coverage = True
        elif suite.get_type() in devtools.ya.test.const.CLANG_COVERAGE_TEST_TYPES and getattr(
            opts, "clang_coverage", False
        ):
            suite_wd_has_resolved_coverage = True
        elif suite.get_type() in ['pytest', "py3test"] and getattr(opts, "python_coverage", False):
            suite_wd_has_resolved_coverage = True
        elif (
            suite.get_type() == 'java'
            and hasattr(suite, 'classpath_package_files')
            and getattr(opts, "java_coverage", False)
        ):
            suite_wd_has_resolved_coverage = True

        if suite_wd_has_resolved_coverage:
            resolved_coverage_paths.append(work_dir)
    output_path = os.path.join("$(BUILD_ROOT)", "merged_coverage.json")
    uid = uid_gen.get_uid(coverage_uids, "coverage_inplace_merger")
    cmd = util_tools.get_test_tool_cmd(opts, "merge_coverage_inplace", suite.global_resources) + [
        "--output",
        output_path,
    ]
    for cov_path in resolved_coverage_paths:
        cmd += ["--coverage-paths", cov_path]
    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": True,
        "broadcast": False,
        "inputs": resolved_coverage_paths,
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(coverage_uids),
        "env": {},
        "target_properties": {},
        "outputs": [output_path],
        'kv': {
            "p": "RC",
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
    return [uid]
