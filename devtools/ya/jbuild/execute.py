import getpass
import socket
import os

import devtools.ya.build.build_opts
import devtools.ya.build.ya_make
import devtools.ya.core.yarg
import yalibrary.tools as tools
import yalibrary.platform_matcher as pm

DEFAULT_JAVA_PATTERN_TOOL_MAP = {'JDK': 'java', 'YMAKE': 'ymake', 'PYTHON': 'python'}


def _conf(platform, pattern_tool_map):
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


def execute(graph, result, opts, app_ctx):
    make_opts = devtools.ya.core.yarg.merge_opts(devtools.ya.build.build_opts.ya_make_options()).params()
    make_opts.__dict__.update(opts.__dict__)
    make_opts.checkout = False
    make_opts.get_deps = None

    pattern_tool_map = DEFAULT_JAVA_PATTERN_TOOL_MAP

    task = {
        'graph': graph,
        'result': result,
        'conf': _conf(pm.my_platform(), pattern_tool_map),
    }

    builder = devtools.ya.build.ya_make.YaMake(make_opts, app_ctx, graph=task, tests=[])

    builder.go()

    return builder.build_result, builder.exit_code
