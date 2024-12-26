import getpass
import socket
import os

import build.build_opts
import build.targets_deref
import build.ya_make
import core.yarg
import yalibrary.tools as tools
import yalibrary.platform_matcher as pm

DEFAULT_JAVA_PATTERN_TOOL_MAP = {'JDK': 'java', 'YMAKE': 'ymake', 'PYTHON': 'python'}


def conf(platform, pattern_tool_map):
    rev = 11111111

    return {
        'cache': True,
        'collect_tasks_diagnostics': True,
        'gsid': os.environ.get('GSID', ''),
        'description': {'host': socket.gethostname(), 'user': getpass.getuser()},
        'keepon': True,
        'path': '/arc/trunk/arcadia',
        'platform': platform,
        'priority': -rev,
        'resources': [
            {'pattern': p, 'resource': tools.resource_id(t, None, platform)} for p, t in pattern_tool_map.items()
        ],
        'revision': rev,
    }


def execute(graph, result, opts, app_ctx, host_platform=None, pattern_tool_map=None, graph_conf=None):
    make_opts = core.yarg.merge_opts(build.build_opts.ya_make_options()).params()
    make_opts.__dict__.update(opts.__dict__)
    make_opts.checkout = False
    make_opts.get_deps = None

    if pattern_tool_map is None:
        pattern_tool_map = DEFAULT_JAVA_PATTERN_TOOL_MAP

    if graph_conf:
        result_conf = graph_conf
        for k, v in pattern_tool_map.items():
            result_conf['resources'].append(
                {
                    'pattern': k,
                    'resource': tools.resource_id(v, None, pm.canonize_full_platform(result_conf['platform'])),
                }
            )
    else:
        result_conf = conf(host_platform or pm.my_platform(), pattern_tool_map)

    task = {
        'graph': graph,
        'result': result,
        'conf': result_conf,
    }

    builder = build.ya_make.YaMake(make_opts, app_ctx, graph=task, tests=[])

    builder.go()

    return builder.build_result, builder.exit_code
