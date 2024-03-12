import core.yarg
import devtools.ya.app.modules.evlog as evlog_module
import devtools.ya.app.modules.params as params_module
import devtools.ya.app.modules.token_suppressions as token_suppressions
import handlers.analyze_make.timeline as timeline
import yalibrary.app_ctx


def execute(action):
    def helper(params):
        ctx = yalibrary.app_ctx.get_app_ctx()

        modules = [
            ('params', params_module.configure(params, False, None)),
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


class AnalyzeMakeYaHandler(core.yarg.CompositeHandler):
    def __init__(self):
        super(AnalyzeMakeYaHandler, self).__init__('Analysis tools for ya make')
        self['timeline'] = core.yarg.OptsHandler(
            action=execute(timeline.main),
            description='Timeline of build events',
            opts=[core.yarg.help.ShowHelpOptions(), EvlogFileOptions()],
        )
