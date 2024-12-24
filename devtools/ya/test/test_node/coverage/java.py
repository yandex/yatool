from . import rigel

import build.gen_plan as gen_plan
import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import exts.path2 as path2
import devtools.ya.test.common as test_common
import devtools.ya.test.const
import devtools.ya.test.test_types.junit as junit
import devtools.ya.test.util.tools as util_tools

COVERAGE_MERGED_TAR_FILENAME = "java.coverage-merged.tar"
CREATE_COVERAGE_REPORT_SCRIPT = "$(SOURCE_ROOT)/build/scripts/create_jcoverage_report.py"


def inject_jacoco_report_nodes(graph, tests, source_filename, opts=None, add_to_result=True):
    cache_nodes = True
    uids = []

    for suite in tests:
        if suite.get_type() != junit.JAVA_TEST_TYPE:
            continue
        uid = uid_gen.get_random_uid("jacoco-report")
        script_path = "$(SOURCE_ROOT)/build/scripts/extract_jacoco_report.py"
        output_path = "$(BUILD_ROOT)/{}/report.exec".format(suite.project_path)
        tar_path = suite.work_dir(source_filename)
        result_cmd = [
            script_path,
            '--archive',
            tar_path,
            '--source-re',
            '^(\\./)?report.*exec$',
            '--destination',
            output_path,
        ]
        node = {
            "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
            "cache": cache_nodes,
            "broadcast": False,
            "inputs": [script_path],
            "uid": uid,
            "cwd": "$(BUILD_ROOT)",
            "priority": 0,
            "deps": testdeps.unique(suite.result_uids),
            "env": {},
            "target_properties": {
                "module_lang": suite.meta.module_lang,
            },
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
                }
            ],
        }
        graph.append_node(node, add_to_result=add_to_result)
        uids += uid
    return uids


def inject_create_java_coverage_report_node(
    graph, merge_node_uid, coverage_prefix_filter, coverage_exclude_regexp, opts, jdk_resource, jacoco_resource
):
    output_path = "$(BUILD_ROOT)/java.coverage.report.tar"

    uid = uid_gen.get_random_uid("java-coverage-report")

    jars, jars_uids = set(), set()

    for node in graph.get_graph()['graph']:
        for out in node['outputs']:
            if not out.endswith('.jar'):
                continue

            if path2.path_startswith(out, '$(BUILD_ROOT)/contrib/java') and not opts.enable_java_contrib_coverage:
                continue

            jars.add(out)
            jars_uids.add(node['uid'])

    import jbuild.gen.actions.compile

    jars_file = '$(BUILD_ROOT)/cls.lst'
    jars_file_cmds = jbuild.gen.actions.compile.make_build_file(sorted(jars), ' ', jars_file)

    cmds = []
    for cmd in jars_file_cmds:
        kv = {'cmd_args': cmd.cmd}

        if cmd.cwd:
            kv['cwd'] = cmd.cwd

        cmds.append(kv)

    coverage_file = "$(BUILD_ROOT)/{}".format(COVERAGE_MERGED_TAR_FILENAME)
    create_report_cmd = get_create_java_coverage_command(
        coverage_file,
        jars_file,
        output_path,
        output_format="html",
        raw_output=False,
        agent_disposition=jacoco_resource,
        prefix_filter=coverage_prefix_filter,
        exclude_regexp=coverage_exclude_regexp,
        opts=opts,
        jdk_resource=jdk_resource,
    )
    cmds.append({'cmd_args': create_report_cmd, 'cwd': '$(BUILD_ROOT)'})

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": [CREATE_COVERAGE_REPORT_SCRIPT],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": [merge_node_uid] + sorted(jars_uids),
        "env": {},
        "target_properties": {},
        "outputs": [output_path],
        "tared_outputs": [output_path],
        'kv': {
            "p": "CV",
            "pc": 'light-cyan',
            "show_out": True,
        },
        "cmds": cmds,
    }
    graph.append_node(node, add_to_result=True)

    return uid


def get_create_java_coverage_command(
    coverage_file,
    jars_file,
    output_path,
    output_format,
    raw_output,
    prefix_filter,
    exclude_regexp,
    agent_disposition,
    opts,
    jdk_resource,
):
    cmd = test_common.get_python_cmd(opts=opts)
    cmd += [
        CREATE_COVERAGE_REPORT_SCRIPT,
        '--source',
        coverage_file,
        '--output',
        output_path,
        '--java',
        util_tools.jdk_tool('java', jdk_resource),
        '--jars-list',
        jars_file,
        '--runner-path',
        'devtools/junit-runner/devtools-junit-runner.jar',
        '--runner-path',
        'devtools/junit5-runner/devtools-junit5-runner.jar',
        '--output-format',
        output_format,
    ]
    if raw_output:
        cmd += ['--raw-output']
    if agent_disposition:
        cmd += ['--agent-disposition', util_tools.jacoco_agent_tool(agent_disposition)]
    if prefix_filter:
        cmd += ['--prefix-filter', prefix_filter]
    if exclude_regexp:
        cmd += ['--exclude-filter', exclude_regexp]
    return cmd


def inject_java_coverage_nodes(graph, suites, resolvers_map, opts, platform_descriptor):
    coverage_prefix_filter = getattr(opts, 'coverage_prefix_filter', None)
    coverage_exclude_regexp = getattr(opts, 'coverage_exclude_regexp', None)

    resolved_filename = devtools.ya.test.const.JAVA_COVERAGE_RESOLVED_FILE_NAME
    result = []
    for suite in [s for s in suites if s.get_type() == "java" and hasattr(s, 'classpath_package_files')]:
        uid = inject_java_coverage_resolve_node(
            graph,
            suite,
            'java.coverage.tar',
            resolved_filename,
            coverage_prefix_filter,
            coverage_exclude_regexp,
            opts=opts,
        )
        result.append(uid)

        if resolvers_map is not None:
            resolvers_map[uid] = (resolved_filename, suite)

    jdk_resource = None
    for s in suites:
        jdk_resource = getattr(s, 'jdk_resource', None)
        if jdk_resource:
            break

    if getattr(opts, 'build_coverage_report', False) and jdk_resource:
        jacoco_resource = None
        for s in suites:
            jacoco_resource = getattr(s, 'jacoco_agent_resource', None)
            if jacoco_resource:
                break
        jcoverage_node_uid = rigel.inject_coverage_merge_node(
            graph, suites, 'java.coverage.tar', COVERAGE_MERGED_TAR_FILENAME, opts=opts
        )
        inject_jacoco_report_nodes(graph, suites, 'java.coverage.tar', opts=opts)
        result += [
            inject_create_java_coverage_report_node(
                graph,
                jcoverage_node_uid,
                coverage_prefix_filter,
                coverage_exclude_regexp,
                opts,
                jdk_resource,
                jacoco_resource,
            )
        ]

    return result


def inject_java_coverage_resolve_node(
    graph, suite, coverage_tar_name, result_filename, prefix_filter, exclude_regexp, opts=None
):
    test_uids = suite.result_uids
    # We need cpf and cpsf files to be arrived
    for dep in suite.get_build_dep_uids():
        test_uids.append(dep)

    uid = uid_gen.get_uid(test_uids, "resolve_javacov")

    coverage_filename = suite.work_dir(coverage_tar_name)
    classpath_file = suite.classpath_file
    report_dir = suite.work_dir("java_report")

    # get vanilla coverage report
    create_report_cmd = get_create_java_coverage_command(
        coverage_filename,
        classpath_file,
        report_dir,
        output_format="json",
        raw_output=True,
        agent_disposition=getattr(suite, 'jacoco_agent_resource'),
        prefix_filter=None,
        exclude_regexp=None,
        opts=opts,
        jdk_resource=getattr(suite, 'jdk_resource'),
    )

    inputs = [coverage_filename, classpath_file]
    output_path = suite.work_dir(result_filename)
    log_path = suite.work_dir("javacov_resolve.log")
    # resolve coverage report
    resolve_cmd = util_tools.get_test_tool_cmd(opts, "resolve_java_coverage", suite.global_resources) + [
        "--coverage-path",
        report_dir + "/report.json",
        "--output",
        output_path,
        "--log-path",
        log_path,
        "--source-root",
        "$(SOURCE_ROOT)",
    ]

    if prefix_filter:
        resolve_cmd += ["--prefix-filter", prefix_filter]

    if exclude_regexp:
        resolve_cmd += ["--exclude-regexp", exclude_regexp]

    inputs += suite.classpath_package_files
    for filename in suite.classpath_package_files:
        resolve_cmd += ["--cpsf", filename]

    node_cmds = [
        {
            "cmd_args": create_report_cmd,
            "cwd": "$(BUILD_ROOT)",
        },
        {
            "cmd_args": resolve_cmd,
            "cwd": "$(BUILD_ROOT)",
        },
    ]

    if suite.has_prepare_test_cmds():
        extra_cmds, extra_inputs = suite.get_prepare_test_cmds()
        inputs += extra_inputs
        node_cmds = extra_cmds + node_cmds

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": True,
        "broadcast": False,
        "inputs": inputs,
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
            # Resolve Java
            "p": "RJ",
            "pc": 'cyan',
            "show_out": True,
        },
        "requirements": gen_plan.get_requirements(
            opts,
            {
                "cpu": 3,
                "network": "restricted",
            },
        ),
        "cmds": node_cmds,
    }
    graph.append_node(node, add_to_result=True)
    return uid
