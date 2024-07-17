import core.yarg
import devtools.ya.app.modules.evlog as evlog_module
import devtools.ya.app.modules.params as params_module
import devtools.ya.app.modules.token_suppressions as token_suppressions
import handlers.analyze_make.timeline as timeline
import handlers.analyze_make.timebloat as timebloat
import os
import yalibrary.app_ctx
import yalibrary.tools
import app_config


def execute(action):
    def helper(params):
        ctx = yalibrary.app_ctx.get_app_ctx()

        modules = [
            ('params', params_module.configure(params, False)),
            ('hide_token', token_suppressions.configure(ctx)),
            ('hide_token2', token_suppressions.configure(ctx)),
            ('evlog', evlog_module.configure(ctx)),
        ]

        with ctx.configure(modules):
            return action(ctx.params)

    return helper


class EvlogFileOptions(core.yarg.Options):
    def __init__(self):
        super(EvlogFileOptions, self).__init__()
        self.analyze_evlog_file = None
        self.analyze_distbuild_json_file = None
        self.detailed = False

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['--evlog'], help='Event log file', hook=core.yarg.SetValueHook('analyze_evlog_file')
            ),
            core.yarg.ArgConsumer(
                ['--distbuild-json-from-yt'],
                help='Event log file',
                hook=core.yarg.SetValueHook('analyze_distbuild_json_file'),
            ),
            core.yarg.ArgConsumer(
                ['--detailed'], help='Draw detailed data', hook=core.yarg.SetConstValueHook('detailed', True)
            ),
        ]


class AnalyzeYaMakeOpts(core.yarg.Options):
    def __init__(self):
        super(AnalyzeYaMakeOpts, self).__init__()
        self.print_path = False
        self.args = []

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['--print-path'],
                help='print the path to analyze-make executable',
                hook=core.yarg.SetConstValueHook('print_path', True),
            ),
            core.yarg.FreeArgConsumer(help='analyze-make args', hook=core.yarg.ExtendHook('args')),
        ]


def run_analyze_make_task_contention(params):
    exe = yalibrary.tools.tool('analyze-make')
    if params.print_path:
        print(exe)
    else:
        cmd = [exe, 'task-contention'] + params.args
        os.execv(exe, cmd)


def basic_options():
    return [
        core.yarg.help.ShowHelpOptions(),
        EvlogFileOptions(),
    ]


class AnalyzeMakeYaHandler(core.yarg.CompositeHandler):
    def __init__(self):
        super(AnalyzeMakeYaHandler, self).__init__('Analysis tools for ya make')
        self['timeline'] = core.yarg.OptsHandler(
            action=execute(timeline.main),
            description='Timeline of build events',
            opts=basic_options(),
        )
        self['timebloat'] = core.yarg.OptsHandler(
            action=execute(timebloat.main),
            description='build events in bloat format',
            opts=basic_options(),
        )
        if app_config.in_house:
            self['task-contention'] = core.yarg.OptsHandler(
                action=run_analyze_make_task_contention,
                description='Plot waiting processes with plotly',
                opts=basic_options() + [AnalyzeYaMakeOpts()],
                unknown_args_as_free=True,
            )
