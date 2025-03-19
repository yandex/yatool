import devtools.ya.core.yarg
import devtools.ya.app.modules.evlog as evlog_module
import devtools.ya.app.modules.params as params_module
import devtools.ya.app.modules.token_suppressions as token_suppressions
import devtools.ya.handlers.analyze_make.graph_diff as graph_diff
import devtools.ya.handlers.analyze_make.timeline as timeline
import devtools.ya.handlers.analyze_make.timebloat as timebloat
import os
import devtools.ya.yalibrary.app_ctx
import yalibrary.tools
import app_config


def execute(action):
    def helper(params):
        ctx = devtools.ya.yalibrary.app_ctx.get_app_ctx()

        modules = [
            ('params', params_module.configure(params, False)),
            ('hide_token', token_suppressions.configure(ctx)),
            ('hide_token2', token_suppressions.configure(ctx)),
            ('evlog', evlog_module.configure(ctx)),
        ]

        with ctx.configure(modules):
            return action(ctx.params)

    return helper


class EvlogFileOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        super().__init__()
        self.analyze_evlog_file = None
        self.analyze_distbuild_json_file = None
        self.detailed = False

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--evlog'], help='Event log file', hook=devtools.ya.core.yarg.SetValueHook('analyze_evlog_file')
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--distbuild-json-from-yt'],
                help='Event log file',
                hook=devtools.ya.core.yarg.SetValueHook('analyze_distbuild_json_file'),
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--detailed'],
                help='Draw detailed data',
                hook=devtools.ya.core.yarg.SetConstValueHook('detailed', True),
            ),
        ]


class AnalyzeYaMakeOpts(devtools.ya.core.yarg.Options):
    def __init__(self):
        super().__init__()
        self.print_path = False
        self.args = []

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--print-path'],
                help='print the path to analyze-make executable',
                hook=devtools.ya.core.yarg.SetConstValueHook('print_path', True),
            ),
            devtools.ya.core.yarg.FreeArgConsumer(
                help='analyze-make args', hook=devtools.ya.core.yarg.ExtendHook('args')
            ),
        ]


class GraphDiffOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.find_diff_target_uids: list[str] = []
        self.find_diff_target_output: str = ''
        self.graphs: list[str] = []
        self.compare_dest_dir: str = ''

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.FreeArgConsumer(
                help='paths to 2 json graph files', hook=devtools.ya.core.yarg.ExtendHook(name='graphs')
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--target-uid'],
                help='target uid for comparison (none or 2 required)',
                hook=devtools.ya.core.yarg.SetAppendHook('find_diff_target_uids'),
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--target-output'],
                help='Output to identify uids for comparison',
                hook=devtools.ya.core.yarg.SetValueHook('find_diff_target_output'),
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--compare-dest-dir'],
                help='Destination directory for comparison info',
                hook=devtools.ya.core.yarg.SetValueHook('compare_dest_dir'),
            ),
        ]

    def postprocess(self):
        if len(self.graphs) != 2:
            raise devtools.ya.core.yarg.ArgsValidatingException("Must specify exactly 2 graph files")
        if self.find_diff_target_uids and len(self.find_diff_target_uids) != 2:
            raise devtools.ya.core.yarg.ArgsValidatingException("Must specify exactly 2 target uids")


class TimeBloatOpts(devtools.ya.core.yarg.Options):
    def __init__(self) -> None:
        super().__init__()
        self.show_leaf_nodes = False
        self.file_filters = []
        self.threshold = 0.1

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--show-leaf-nodes'],
                help='Display leaf nodes (compilation/linking/etc) in dispatch_build stage. Use with caution (or filters) on large evlogs',
                hook=devtools.ya.core.yarg.SetConstValueHook('show_leaf_nodes', True),
                group=devtools.ya.core.yarg.FILTERS_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--file-filter'],
                help="Show only build nodes that match <file-filter>. Syntax is the same as in --test-filter",
                hook=devtools.ya.core.yarg.SetAppendHook('file_filters'),
                group=devtools.ya.core.yarg.FILTERS_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--threshold-sec'],
                help="Do not show build nodes that were shorter than threshold",
                hook=devtools.ya.core.yarg.SetValueHook('threshold', transform=float),
                group=devtools.ya.core.yarg.FILTERS_OPT_GROUP,
            ),
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
        devtools.ya.core.yarg.help.ShowHelpOptions(),
        EvlogFileOptions(),
    ]


class AnalyzeMakeYaHandler(devtools.ya.core.yarg.CompositeHandler):
    def __init__(self):
        super().__init__('Analysis tools for ya make')
        self['timeline'] = devtools.ya.core.yarg.OptsHandler(
            action=execute(timeline.main),
            description='Timeline of build events',
            opts=basic_options(),
        )
        self['timebloat'] = devtools.ya.core.yarg.OptsHandler(
            action=execute(timebloat.main),
            description='build events in bloat format',
            opts=basic_options() + [TimeBloatOpts()],
        )
        self['graph-diff'] = devtools.ya.core.yarg.OptsHandler(
            action=execute(graph_diff.diff),
            description='find diff between two json graphs',
            opts=[devtools.ya.core.yarg.help.ShowHelpOptions(), GraphDiffOptions()],
            examples=[
                devtools.ya.core.yarg.UsageExample('{prefix} <graph1> <graph2>', 'Create comparison info files in cwd'),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} <graph1> <graph2> --compare-dest-dir <dir_path>',
                    'Create comparison info files in <dir_path>',
                ),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} <graph1> <graph2> --target-uid <uid1> --target-uid <uid2>',
                    'Print diff info between <uid1> and <uid2>',
                ),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} <graph1> <graph2> --target-output <output>',
                    'Print diff info for uids with output <output>',
                ),
            ],
            unknown_args_as_free=False,
        )
        if app_config.in_house:
            self['task-contention'] = devtools.ya.core.yarg.OptsHandler(
                action=run_analyze_make_task_contention,
                description='Plot waiting processes with plotly',
                opts=basic_options() + [AnalyzeYaMakeOpts()],
                unknown_args_as_free=True,
            )
