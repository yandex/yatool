import os
import six
import time

import devtools.ya.yalibrary.app_ctx
import devtools.ya.build.gen_plan as gen_plan
import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import library.python.func
import devtools.ya.test.const
import devtools.ya.test.util.tools as util_tools


@library.python.func.lazy
def get_graph_timestamp():
    return int(os.environ.get('YA_GRAPH_TIMESTAMP', time.time()))


@library.python.func.lazy
def get_coverage_table_chunks_count():
    return int(os.environ.get('YA_COVERAGE_TABLE_CHUNKS', devtools.ya.test.const.COVERAGE_TABLE_CHUNKS))


def get_svn_version():
    return int(devtools.ya.yalibrary.app_ctx.get_app_ctx().revision)


def get_upload_yt_table_root(arc_root, snap_shot_name):
    snap_shot_name = snap_shot_name or str(get_svn_version())
    return "/".join([devtools.ya.test.const.COVERAGE_YT_ROOT_PATH, "v1", snap_shot_name, str(get_graph_timestamp())])


def get_upload_yt_table_path(yt_path, chunk):
    return "{}/{}_{}".format(yt_path, devtools.ya.test.const.COVERAGE_YT_TABLE_PREFIX, chunk)


# create root node only once (otherwise may hit the queue limit for yt account)
def create_yt_root_maker_node(arc_root, graph, nchunks, global_resources, opts):
    yt_table_root = opts.upload_coverage_yt_path or get_upload_yt_table_root(
        arc_root, opts.coverage_upload_snapshot_name
    )
    tables = [get_upload_yt_table_path(yt_table_root, chunk) for chunk in range(nchunks)]

    node_log_path = "$(BUILD_ROOT)/coverage_create_root.txt"
    node_cmd = util_tools.get_test_tool_cmd(opts, "upload_coverage", global_resources) + [
        "--log-path",
        node_log_path,
    ]

    if opts.coverage_yt_token_path:
        node_cmd += ['--yt-token-path', opts.coverage_yt_token_path]

    if opts.upload_coverage_yt_proxy:
        node_cmd += ['--yt-proxy', opts.upload_coverage_yt_proxy]

    # args for subcommand
    node_cmd += [
        "create_root",
        "--yt-root-path",
        yt_table_root,
        "--tables",
    ] + tables

    # Least surprise behaviour - don't cache create table node if tests are not cached
    cacheable = opts.cache_tests

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": [],
        "uid": uid_gen.get_uid([yt_table_root], 'coverage_create_table'),
        "cwd": "$(BUILD_ROOT)",
        "env": {
            "YT_PROXY": devtools.ya.test.const.COVERAGE_YT_PROXY,
        },
        "secrets": ['YA_COVERAGE_YT_TOKEN'],
        "priority": 0,
        "cache": cacheable,
        "target_properties": {},
        "outputs": [node_log_path],
        "deps": [],
        "kv": {
            "p": "UL",
            "pc": "light-cyan",
            "show_out": True,
        },
        "requirements": gen_plan.get_requirements(opts, {"network": "full"}),
        "cmds": [
            {
                "cmd_args": node_cmd,
                "cwd": "$(BUILD_ROOT)",
            },
        ],
    }
    graph.append_node(node)
    return node["uid"]


def create_coverage_upload_node(arc_root, graph, suite, covname, deps, chunk, opts):
    test_out_path = suite.work_dir()
    input_file = os.path.join(test_out_path, covname)
    node_log_path = os.path.join(test_out_path, os.path.splitext(covname)[0] + "_upload.log")
    yt_table_root = opts.upload_coverage_yt_path or get_upload_yt_table_root(
        arc_root, opts.coverage_upload_snapshot_name
    )
    yt_table_path = get_upload_yt_table_path(yt_table_root, chunk)
    node_uid = uid_gen.get_uid(deps + [yt_table_path], "coverage_upload")
    cmds = []

    if opts and opts.coverage_direct_upload_yt:
        stool_cmd = util_tools.get_test_tool_cmd(opts, "upload_coverage", suite.global_resources) + [
            "--log-path",
            node_log_path,
        ]

        if opts and opts.coverage_yt_token_path:
            stool_cmd += ['--yt-token-path', opts.coverage_yt_token_path]

        if opts.upload_coverage_yt_proxy:
            stool_cmd += ['--yt-proxy', opts.upload_coverage_yt_proxy]

        # args for subcommand
        stool_cmd += [
            "upload",
            "--yt-table-path",
            yt_table_path,
            "--project-path",
            suite.project_path,
            "--test-type",
            suite.get_type(),
            "--coverage-report",
            input_file,
        ]

        cmds.append({"cmd_args": stool_cmd, "cwd": "$(BUILD_ROOT)"})

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": [input_file],
        "uid": node_uid,
        "cwd": "$(BUILD_ROOT)",
        "env": {
            "YT_PROXY": devtools.ya.test.const.COVERAGE_YT_PROXY,
        },
        "secrets": ['YA_COVERAGE_YT_TOKEN'],
        "priority": 0,
        "deps": testdeps.unique(deps),
        "cache": True,
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "outputs": [node_log_path],
        "kv": {
            "p": "UL",
            "pc": "light-cyan",
            "show_out": True,
        },
        "requirements": gen_plan.get_requirements(opts, {"network": "full"}),
        "cmds": cmds,
        "tags": ['coverage_upload_node'],
    }
    if int(os.environ.get("YA_COVERAGE_TAG_UPLOAD_NODES", "1")):
        node["tag"] = "coverage_yt_upload"
    graph.append_node(node, add_to_result=True)
    return node["uid"]


def inject_coverage_upload_nodes(arc_root, graph, resolve_uids_to_suite, create_table_node_uid, opts):
    # check for input intersections
    type_to_name = {}
    for covfilename, suite in six.itervalues(resolve_uids_to_suite):
        sid = suite.get_suite_id()
        if sid not in type_to_name:
            type_to_name[sid] = set()
        assert (
            covfilename not in type_to_name[sid]
        ), 'Looks like resolve files from different coverage types may overwritten each other: %s %s' % (
            sid,
            covfilename,
        )
        type_to_name[sid].add(covfilename)

    upload_uids = []
    coverage_nchunks = get_coverage_table_chunks_count()
    for suite_n, (resolve_uid, (covfilename, suite)) in enumerate(six.iteritems(resolve_uids_to_suite)):
        upload_uids += [
            create_coverage_upload_node(
                arc_root,
                graph,
                suite,
                covfilename,
                [resolve_uid] + ([create_table_node_uid] if create_table_node_uid else []),
                suite_n % coverage_nchunks,
                opts,
            )
        ]
    return upload_uids
