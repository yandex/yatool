from . import helpers

import devtools.ya.app
import devtools.ya.core.yarg
from devtools.ya.build import build_opts
import devtools.ya.test.opts as test_opts


def default_options():
    return [
        build_opts.BuildTargetsOptions(with_free=True),
        build_opts.BeVerboseOptions(),
        build_opts.ShowHelpOptions(),
        build_opts.YMakeDebugOptions(),
        build_opts.YMakeBinOptions(),
        build_opts.YMakeRetryOptions(),
        build_opts.FlagsOptions(),
        build_opts.SandboxAuthOptions(),
    ]


class JavaYaHandler(devtools.ya.core.yarg.CompositeHandler):
    def __init__(self):
        super().__init__(description='Java build helpers')

        self['dependency-tree'] = devtools.ya.core.yarg.OptsHandler(
            action=devtools.ya.app.execute(action=helpers.print_ymake_dep_tree),
            description='Print dependency tree',
            opts=default_options() + [build_opts.BuildTypeOptions('release')],
            visible=True,
        )
        self['classpath'] = devtools.ya.core.yarg.OptsHandler(
            action=devtools.ya.app.execute(action=helpers.print_classpath),
            description='Print classpath',
            opts=default_options() + [build_opts.BuildTypeOptions('release')],
            visible=True,
        )
        self['test-classpath'] = devtools.ya.core.yarg.OptsHandler(
            action=devtools.ya.app.execute(action=helpers.print_test_classpath),
            description='Print run classpath for test module',
            opts=default_options() + [test_opts.RunTestOptions()],
            visible=True,
        )
        self['find-all-paths'] = devtools.ya.core.yarg.OptsHandler(
            action=devtools.ya.app.execute(action=helpers.find_all_paths),
            description='Find all PEERDIR paths of between two targets',
            opts=default_options() + [build_opts.FindPathOptions()],
            visible=True,
        )
