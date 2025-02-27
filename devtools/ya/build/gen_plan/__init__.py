import base64
import copy

import exts.func
import exts.yjson as json
import exts.asyncthread as core_async
import exts.windows
import exts.hashing as hashing
import getpass
import logging
import os
import platform
import exts.process as process
from subprocess import CalledProcessError

import devtools.ya.build.ymake2 as ymake2
import devtools.ya.core.gsid
from exts.strtobool import strtobool
import yalibrary.platform_matcher as pm
import yalibrary.vcs.vcsversion as vcsversion
from devtools.ya.yalibrary.yandex.distbuild import distbs_consts
import yalibrary.tools as tools
import devtools.ya.core.config as config
import devtools.ya.core.patch_tools as patch_tools

logger = logging.getLogger(__name__)
TRUNK_PATH = '/arc/trunk/arcadia'
DISTBUILD_API_VERSION = 2


def gen_plan(opts):
    return _gen_arc_graph_file(opts)


def make_tared_repositories_config(tared_repo):
    return [
        {
            "repository": 'tar:{}'.format(tared_repo),
            "pattern": "$(SOURCE_ROOT)",
        }
    ]


def _is_branch(path):
    return TRUNK_PATH not in path


def _make_repositories_config(
    for_uid,
    root,
    revision,
    arcadia_svn_path,
    patch,
    repository_type,
    source_root_pattern,
    arc_url,
):
    if repository_type == distbs_consts.DistbuildRepoType.TARED:
        return make_tared_repositories_config(True)

    if revision is None:
        revision, arcadia_svn_path = vcsversion.repo_config(root)

    arcadia_svn_path = arcadia_svn_path or TRUNK_PATH  # TODO: compat

    pf, p = '', ''
    if patch:
        trs, data = patch_tools.convert_patch_spec(patch)
        pf, p = patch_tools.combine_transformations(trs), data

    # prepare arc repo
    if arc_url:
        return [
            {
                "pattern": "$({})".format(None if for_uid else source_root_pattern),
                "patch_filters": pf,
                "patch": p,
                "use_arcc": True,
                "arc_url": arc_url,
            }
        ]
    else:
        # prepare svn repo
        return [
            {
                "repository": "svn:/" + arcadia_svn_path,
                "pattern": "$({})".format(None if for_uid else source_root_pattern),
                "revision": revision,
                "patch_filters": pf,
                "patch": p,
                "use_arcc": repository_type == distbs_consts.DistbuildRepoType.ARCC,
            }
        ]


# Don't do slow libc version discovering for Linux
def _platform():
    if platform.system() == 'Linux':
        uname = platform.uname()
        # system, release, machine
        return '-'.join([uname[0], uname[2], uname[4]])
    else:
        return platform.platform()


def gen_description():
    return {
        'gsid': devtools.ya.core.gsid.flat_session_id(),
        'description': {
            'platform': _platform(),
            'host': platform.node(),
            'user': getpass.getuser(),
        },
    }


def _gen_extra_dict(
    for_uid,
    root,
    revision,
    arcadia_svn_path,
    priority,
    patch,
    cluster,
    coordinators_filter,
    force_coordinators_filter,
    cache_namespace,
    build_execution_time,
    repository_type,
    source_root_pattern,
    distbuild_pool,
    arc_url,
    default_node_requirements,
):
    repos = _make_repositories_config(
        for_uid,
        root,
        revision,
        arcadia_svn_path,
        patch,
        repository_type,
        source_root_pattern,
        arc_url,
    )
    conf = {
        'repos': repos,
        'collect_tasks_diagnostics': True,
        'namespace': cache_namespace,
        'api_version': DISTBUILD_API_VERSION,
    }
    if default_node_requirements:
        conf['default_node_requirements'] = default_node_requirements
    if build_execution_time:
        conf['max_execution_time'] = build_execution_time

    if not for_uid:
        conf.update(gen_description())
        conf.update(
            {
                'coordinator': coordinators_filter or '',
                'pool': distbuild_pool or '',
                'cluster': cluster or '',
                'priority': int(priority or 0),
                'force_coordinators_filter': force_coordinators_filter,
            }
        )
    return conf


def gen_extra_dict_by_opts(
    opts,
    for_uid=False,
    repository_type=None,
    priority=None,
    cluster=None,
    coordinators_filter=None,
    distbuild_pool=None,
):
    return _gen_extra_dict(
        for_uid,
        getattr(opts, 'arc_root', None),
        getattr(opts, 'revision_for_check', None),
        getattr(opts, 'svn_url_for_check', None),
        priority or getattr(opts, 'dist_priority', 0),
        getattr(opts, 'distbuild_patch', None),
        cluster or getattr(opts, 'distbuild_cluster', None),
        coordinators_filter or getattr(opts, 'coordinators_filter', None),
        getattr(opts, 'force_coordinators_filter', False),
        getattr(opts, 'cache_namespace', None),
        getattr(opts, 'build_execution_time', None),
        repository_type,
        getattr(opts, 'build_graph_source_root_pattern', 'SOURCE_ROOT'),
        distbuild_pool or getattr(opts, 'distbuild_pool', None),
        getattr(opts, 'arc_url_as_working_copy_in_distbuild', None),
        getattr(opts, 'default_node_requirements', None),
    )


def _gen_arc_graph_file(opts):
    return _gen_graph_file(opts, gen_extra_dict_by_opts(opts))


@exts.func.memoize()
def get_ya_bin_resource(arc_root, platform):
    ya_path = os.path.join(arc_root, 'ya.bat' if exts.windows.on_win() else 'ya')
    if not os.path.isfile(ya_path):
        logger.debug("ya was not found in {}".format(arc_root))
        return None

    try:
        args = ["--print-sandbox-id={}".format(platform.lower())]
        out = process.run_process(ya_path, args).strip()
        logger.debug("ya --print-sandbox-id={} returned {}".format(platform, out))
        res_id = 'sbr:' + str(int(out))
        return 'YA-BIN-{}'.format(res_id), res_id
    except (CalledProcessError, ValueError):
        logger.debug("ya did not return valid sandbox id")
        return None


@exts.func.memoize()
def get_tool_resource(tool_name, platform):
    res_id = tools.resource_id(tool_name, None, platform)
    return '{}-{}'.format(tool_name.upper(), res_id), res_id if res_id else None


def get_python_resource(platform):
    return get_tool_resource('python', platform)


def get_ymake_bin_resource(platform):
    return get_tool_resource('ymake', platform)


def get_uc_resource(platform):
    return get_tool_resource('uc', platform)


def add_ya_bin_resource(opts):
    return not getattr(opts, 'no_yabin_resource', False)


def add_ymake_bin_resource(opts):
    return not getattr(opts, 'no_ymake_resource', False)


def _meta_by_platform(opts, platform, add_compilers=True, add_uc=False):
    meta = {
        "resources": [],
        "cache": not opts.clear_build,
        "keepon": opts.continue_on_fail,
    }

    python_pattern, python_resource_id = get_python_resource(platform)
    meta["resources"].append(
        {
            "resource": python_resource_id,
            "pattern": python_pattern,
        }
    )

    add_ymake = add_ymake_bin_resource(opts)
    if add_ymake:
        ymake_pattern, ymake_resource_id = get_ymake_bin_resource(platform)
        meta["resources"].append({"pattern": ymake_pattern, "resource": ymake_resource_id})

    add_yabin = add_ya_bin_resource(opts)
    ya_bin_resource = get_ya_bin_resource(opts.arc_root, platform) if add_yabin else None
    if ya_bin_resource:
        ya_bin_pattern, ya_bin_resource_id = ya_bin_resource
        meta["resources"].append({"pattern": ya_bin_pattern, "resource": ya_bin_resource_id})

    if add_uc:
        uc_pattern, uc_resource_id = get_uc_resource(platform)
        meta["resources"].append({"pattern": uc_pattern, "resource": uc_resource_id})

    if add_compilers:
        cxx_id = tools.resource_id('c++', None, platform)
        meta["resources"].append({"pattern": tools.param("cc", None, param="match_root"), "resource": cxx_id})
        meta["resources"].append(
            {"pattern": "DEFAULT_LINUX_X86_64", "resource": cxx_id},  # XXX: hack
        )

    return meta


def _gen_graph_file(opts, extra_dict=None):
    # XXX
    params = copy.copy(opts.__dict__)
    params.pop('mode')
    if 'dump_graph' in params:
        params.pop('dump_graph')

    from devtools.ya.build.graph import get_version_info

    vcs_info = core_async.future(
        lambda: get_version_info(
            opts.arc_root,
            getattr(opts, 'bld_root', getattr(opts, 'custom_build_directory')),
            opts.vcs_file,
            opts.flags,
            custom_version=getattr(opts, 'custom_version', ''),
        ),
        daemon=False,
    )
    stdout = ymake2.ymake_gen_graph(mode='dist', dump_graph='json', **params)[0].stdout
    jgraph = json.loads(stdout)

    platform = jgraph['conf'].get('platform')
    if platform:
        jgraph['conf'].update(_meta_by_platform(opts, platform=platform))
    if extra_dict:
        jgraph['conf'].update(extra_dict)

    jgraph['conf'].update({'resources': [vcs_info()]})

    return jgraph


def gen_conf(
    params,
    platform,
    priority,
    cluster,
    coordinators_filter,
    for_uid=False,
    repository_type=None,
    distbuild_pool=None,
):
    conf = {'platform': platform, 'graph_size': 1}
    conf.update(_meta_by_platform(params, platform, add_compilers=False))
    conf.update(
        gen_extra_dict_by_opts(
            params,
            for_uid=for_uid,
            repository_type=repository_type,
            priority=priority,
            cluster=cluster,
            coordinators_filter=coordinators_filter,
            distbuild_pool=distbuild_pool,
        )
    )
    return conf


def gen_stats_uid(params, platform, tags, outputs, node_type):
    try:
        partitions_id = params.flags.get("RECURSE_PARTITION_INDEX", "0")
        partitions_count = params.flags.get("RECURSE_PARTITIONS_COUNT", "1")
        path_to_arcadia = params.arc_root
        path_to_autocheck_dir = os.path.join(path_to_arcadia, "autocheck")
        path_to_balancing_configs_dir = os.path.join(path_to_autocheck_dir, "balancing_configs")
        strs = [
            partitions_id,
            partitions_count,
            platform,
            node_type,
            str(sorted(tags)),
            str(sorted(outputs)),
            str(sorted(params.flags.items())),
            # python can't sort dicts
            str(
                sorted(
                    # for consistent sorting/md5 between ya-bin2 & ya-bin3
                    json.dumps(target_platform, sort_keys=True, indent=0, separators=(',', ':'))
                    for target_platform in params.target_platforms
                )
            ),
        ]

        strs.append(hashing.md5_path(os.path.join(path_to_autocheck_dir, "autocheck.yaml")))

        for filename in sorted(os.listdir(path_to_autocheck_dir)):
            full_filepath = os.path.join(path_to_autocheck_dir, filename)
            if filename.startswith("autocheck-config") and os.path.isfile(full_filepath):
                strs.append(hashing.md5_path(full_filepath))

        for filename in os.listdir(path_to_balancing_configs_dir):
            full_filepath = os.path.join(path_to_balancing_configs_dir, filename)
            if os.path.isfile(full_filepath):
                strs.append(hashing.md5_path(full_filepath))

        return hashing.md5_value(":".join(strs))
    except Exception as e:
        if config.is_test_mode():
            raise
        logger.exception("Something was wrong while building stats_uid: " + repr(e))

    return None


def gen_dummy_graph(
    params,
    cmds,
    env,
    outputs,
    platform,
    requirements,
    timeout,
    secrets=[],
    priority=None,
    cluster=None,
    coordinators_filter=None,
    deps=None,
    tags=None,
    repository_type=None,
    distbuild_pool=None,
):
    node_type = 'GG_GRAPH'
    dep, dep_no_uid = deps or ([], [])
    env_for_uid, env_no_uid = env or ({}, {})
    tags = tags or []
    stats_uid = gen_stats_uid(params, platform, tags, outputs, node_type)

    def merge(a, b):
        result = a.copy()
        result.update(b)
        return result

    def get_node(for_uid=False):
        node = {
            'type': 2,
            'uid': None,
            'priority': 0,
            'broadcast': False,
            'cmds': [
                {'cwd': '$(BUILD_ROOT)', 'cmd_args': (cmd if for_uid else cmd + cmd_no_uid)} for cmd, cmd_no_uid in cmds
            ],
            'deps': dep if for_uid else dep + dep_no_uid,
            'inputs': [],
            'kv': {'p': node_type, 'pc': 'red', 'show_out': False},
            'env': env_for_uid if for_uid else merge(env_for_uid, env_no_uid),
            'outputs': outputs,
            'secrets': secrets,
        }

        if not for_uid and stats_uid:
            node['stats_uid'] = stats_uid

        if timeout:
            node['timeout'] = timeout

        if requirements:
            node['requirements'] = requirements
        return node

    node_for_uid = get_node(for_uid=True)
    try:
        for cmd in node_for_uid.get('cmds', []):
            cmd_args = cmd.get('cmd_args', [])
            for i, arg in enumerate(cmd_args):
                if arg == "--raw-params" and i + 1 < len(cmd_args):
                    logger.debug(
                        "Raw params %s",
                        json.dumps(json.loads(base64.b64decode(cmd_args[i + 1])), indent=2, sort_keys=True),
                    )
                    break

    except Exception:
        logger.debug("Could not parse node")

    conf_for_hash = gen_conf(
        params,
        platform,
        priority,
        cluster,
        coordinators_filter,
        for_uid=True,
        repository_type=repository_type,
        distbuild_pool=distbuild_pool,
    )
    graph_str = json.dumps(
        {'conf': conf_for_hash, 'graph': [node_for_uid]}, sort_keys=True, indent=2, separators=(',', ': ')
    )
    logger.debug("Graph for uid %s", graph_str)

    uid = 'custom-' + hashing.md5_value(graph_str)

    conf = gen_conf(
        params,
        platform,
        priority,
        cluster,
        coordinators_filter,
        for_uid=False,
        repository_type=repository_type,
        distbuild_pool=distbuild_pool,
    )
    node = get_node(for_uid=False)
    node['uid'] = uid
    node['tags'] = tags

    return {'conf': conf, 'graph': [node], 'result': [uid]}


def generate_fetch_from_sandbox_node(uid, res_id, archive, source_root_pattern):
    python_pattern, _ = get_python_resource("linux")
    return {
        'type': 2,
        "uid": uid,
        "priority": 0,
        'broadcast': False,
        "cmds": [
            {
                "cmd_args": [
                    "$({})/python".format(python_pattern),
                    "$({})/build/scripts/fetch_from_sandbox.py".format(source_root_pattern),
                    "--resource-id",
                    res_id,
                    "--resource-file",
                    "$(RESOURCE_ROOT)/sbr/{}/resource".format(res_id),
                    "--copy-to",
                    archive,
                ],
                "cwd": "$(BUILD_ROOT)",
            }
        ],
        "deps": [],
        'env': {
            'YA_VERBOSE_FETCHER': '1',
        },
        "inputs": [
            "$({})/build/scripts/fetch_from.py".format(source_root_pattern),
            "$({})/build/scripts/fetch_from_sandbox.py".format(source_root_pattern),
        ],
        "kv": {"p": "SB", "pc": "yellow", "show_out": False},
        "outputs": [archive],
        "resources": [{"uri": "sbr:{}".format(res_id)}],
        "requirements": {"cpu": 1, "ram": 4, "network": "full"},
        "tags": [],
        "target_properties": {},
        "timeout": 60,
    }


@exts.func.memoize()
def get_fetch_node_for_build_graph_cache(opts):
    if not opts.build_graph_cache_resource:
        return None

    scheme, res_id = opts.build_graph_cache_resource.split(':', 1)
    if scheme != 'sbr':
        logger.debug(
            "Only sbr: scheme is supported in opts.build_graph_cache_resource: %s", opts.build_graph_cache_resource
        )
        return None

    source_root_pattern = opts.build_graph_source_root_pattern
    uid = "YMAKE_CACHE_RES-{}-{}".format(source_root_pattern, res_id)
    archive = "$(BUILD_ROOT)/.build_graph_cache/caches.tar.zst"
    node = generate_fetch_from_sandbox_node(uid, res_id, archive, source_root_pattern)

    return node, uid, archive


@exts.func.memoize()
def get_fetch_node_for_nodes_statistics(opts):
    if not opts.nodes_stat_resource:
        return None

    scheme, res_id = opts.nodes_stat_resource.split(':', 1)
    if scheme != 'sbr':
        logger.debug("Only sbr: scheme is supported in opts.nodes_stat_resource: %s", opts.nodes_stat_resource)
        return None

    source_root_pattern = opts.build_graph_source_root_pattern
    uid = "NODES_STAT_RES-{}-{}".format(source_root_pattern, res_id)
    archive = "$(BUILD_ROOT)/.nodes_statistics/statistics.json"
    node = generate_fetch_from_sandbox_node(uid, res_id, archive, source_root_pattern)

    return node, uid, archive


def get_requirements(opts, extra_reqs=None):
    if opts and getattr(opts, "default_node_requirements"):
        reqs = dict(opts.default_node_requirements)
    else:
        reqs = {}

    if extra_reqs:
        reqs.update(extra_reqs)
    return reqs


def real_build_type(tc, opts):
    return tc.get('build_type') or opts.build_type


PLATFORM_FLAGS = {'MUSL', 'ALLOCATOR', 'FAKEID', 'RACE'}
FLAGS_MAPPING = {'USE_LTO': 'lto', 'USE_THINLTO': 'thinlto', 'MUSL': 'musl', 'USE_AFL': 'AFL', 'RACE': 'race'}


def _fmt_tag(k, v):
    if k == 'SANITIZER_TYPE':
        return v[0] + 'san'

    try:
        yes = strtobool(v)
    except ValueError:
        yes = 0

    if k in FLAGS_MAPPING:
        return FLAGS_MAPPING[k] if yes else None

    return '{k}={v}'.format(k=k, v=v)


# See build_graph_and_tests::iter_target_flags
def prepare_tags(tc, extra_flags, opts):
    tags = []

    flags = copy.deepcopy(tc.get('flags', {}))

    flags.update({k: v for k, v in extra_flags.items() if k in PLATFORM_FLAGS})

    platform = pm.stringize_platform(tc['platform']['target']).lower()

    tags.append(platform)
    tags.append(real_build_type(tc, opts))

    if flags:
        tags.extend(sorted([_f for _f in [_fmt_tag(k, v) for k, v in flags.items()] if _f]))

    return tags, platform


def _calculate_stats_uid(node):
    return hashing.md5_value(
        str(
            [
                node.get('platform', ''),
                str(sorted(node.get('tags', []))),
                node.get('kv', {}).get('p', ''),
                str(sorted(node.get('outputs', []))),
            ]
        )
    )


def _calculate_static_uid(node):
    sep = ':'
    return hashing.md5_value(
        sep.join(sep.join(cmd['cmd_args']) for cmd in node['cmds'])
        + sep
        + (sep.join(sorted(node['outputs'])) if 'outputs' in node else '')
    )


def inject_stats_and_static_uids(graph):
    for node in graph['graph']:
        if 'stats_uid' not in node:
            node['stats_uid'] = _calculate_stats_uid(node)
        if 'static_uid' not in node:
            node['static_uid'] = _calculate_static_uid(node)
