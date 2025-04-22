import logging
import os.path
import subprocess
import sys
import textwrap

import devtools.ya.build.build_opts as build_opts
import devtools.ya.build.graph
import devtools.ya.build.ya_make
import devtools.ya.core.common_opts
import devtools.ya.core.event_handling
import devtools.ya.core.yarg as yarg
import exts.tmp

import devtools.ya.app


logger = logging.getLogger(__name__)


class RunYaHandler(yarg.OptsHandler):
    def __init__(self):
        yarg.OptsHandler.__init__(
            self,
            action=devtools.ya.app.execute(run, respawn=devtools.ya.app.RespawnType.MANDATORY),
            description="Compile and run a target",
            examples=[
                yarg.UsageExample(
                    "{prefix} tools/uc --help",
                    "Build and run 'tools/uc' utility",
                ),
                yarg.UsageExample(
                    "{prefix} -d --musl tools/uc -- --help",
                    "Build 'tools/uc' tool with musl and in debug mode and run it",
                ),
            ],
            extra_help=textwrap.dedent(
                """\
                Two modes are available.
                [[imp]]Simple mode[[rst]]:
                  ya run [YA RUN OPTIONS] [TARGET [ARGS]...]

                [[imp]]Extended mode.[[rst]]
                In this mode you can use all 'ya make' options, but the delimiter '--' is needed to separate the target arguments from the 'ya make' ones:
                  ya run [YA MAKE OPTIONS AND YA RUN OPTIONS] [TARGET] -- [ARGS]...\
                """
            ),
        )

    def handle(self, root_handler, args, prefix):
        if "--" in args:
            i = args.index("--")
            args, free_args = args[:i], args[i + 1 :]
            self._unknown_args_as_free = False
            self._opt = yarg.merge_opts([RunOptsSupplement(free_args)] + _reduced_ya_make_options())
        else:
            self._unknown_args_as_free = True
            self._opt = yarg.merge_opts([RunOptsStandalone(), devtools.ya.core.common_opts.AuthOptions()])

        return super().handle(root_handler, args, prefix)

    def format_usage(self):
        return "[OPTIONS] TARGET [--] [TARGET_ARGS]..."


class RunOptsBase(yarg.Options):
    def __init__(self, ya_run_args=None, build_type=None):
        self.ya_run_build_type = build_type
        self.ya_run_gdb = False
        self.ya_run_args = ya_run_args or []

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                names=["--ya-run-build-type"],
                help="Target build type",
                hook=yarg.SetValueHook(name="ya_run_build_type"),
            ),
            yarg.ArgConsumer(
                names=["--ya-run-gdb"],
                help="Run target with ya tool gdb",
                hook=yarg.SetConstValueHook(name="ya_run_gdb", const=True),
            ),
        ]


class RunOptsStandalone(RunOptsBase):
    def __init__(self):
        self.build_targets = []
        super().__init__(ya_run_args=[], build_type="release")

    @staticmethod
    def consumer():
        return RunOptsBase.consumer() + [
            yarg.FreeArgConsumer(help="ARGS", hook=yarg.ExtendHook("ya_run_args")),
        ]

    def postprocess(self):
        if not self.ya_run_args:
            self.build_targets = [os.getcwd()]
        elif self.ya_run_args[0] in ["-h", "--help"]:
            raise yarg.ShowHelpException()
        else:
            self.build_targets = [self.ya_run_args.pop(0)]


class RunOptsSupplement(RunOptsBase):
    def __init__(self, ya_run_args):
        super().__init__(ya_run_args)


def run(params):
    import app_ctx

    ya_make_opts = yarg.merge_opts(build_opts.ya_make_options())
    opts = yarg.merge_params(ya_make_opts.initialize([]), params)
    opts.show_final_ok = False
    opts.create_symlinks = False

    if opts.ya_run_build_type is not None:
        opts.build_type = opts.ya_run_build_type
    opts.ignore_recurses = True

    if len(opts.rel_targets) > 1:
        _report_and_exit(
            app_ctx,
            "Only single target is allowed but {} specified: {}".format(
                len(opts.rel_targets), ", ".join(opts.rel_targets)
            ),
        )

    if opts.build_threads == 0:
        _report_and_exit(app_ctx, "Build thread count = 0 is not allowed")

    # Some modules read parameters from app_ctx.params so we must keep it up-to-date
    app_ctx.params.update(opts.as_dict())

    subscribers = [
        devtools.ya.build.ya_make.DisplayMessageSubscriber(opts, app_ctx.display),
        devtools.ya.core.event_handling.EventToLogSubscriber(),
    ]

    if getattr(app_ctx, 'evlog', None):
        subscribers.append(
            devtools.ya.build.ya_make.YmakeEvlogSubscriber(app_ctx.evlog.get_writer('ymake')),
        )

    with app_ctx.event_queue.subscription_scope(*subscribers):
        graph = devtools.ya.build.graph.build_graph_and_tests(opts, check=True, display=app_ctx.display)[0]

    bin_rel_path = _get_bin_rel_path(app_ctx, graph, opts.rel_targets[0])

    with exts.tmp.temp_dir() as output_root:
        opts.output_root = output_root

        _build(opts, app_ctx, graph)

        bin_abs_path = os.path.join(output_root, bin_rel_path)
        if not os.path.exists(bin_abs_path):
            _report_and_exit(
                app_ctx, "Build finished successfully but target file '{}' doesn't exist".format(bin_abs_path)
            )
        args = []
        if opts.ya_run_gdb:
            args = [sys.argv[0], "tool", "gdb", "--args"]
        args += [bin_abs_path] + opts.ya_run_args

        sys.exit(subprocess.call(args))


def _get_bin_rel_path(app_ctx, graph, target):
    result = graph["result"]
    if not result:
        _report_and_exit(app_ctx, "Nothing to build in {}".format(target))

    result_uids = set(result)

    result_node = None
    for node in graph["graph"]:
        if node["uid"] in result_uids and node.get("target_properties", {}).get("module_type", None) == "bin":
            result_node = node
            break

    if not result_node:
        _report_and_exit(app_ctx, "Target {} is not a binary".format(target))

    return result_node["outputs"][0].replace("$(BUILD_ROOT)/", "")


def _build(opts, app_ctx, graph):
    builder = devtools.ya.build.ya_make.YaMake(opts, app_ctx, graph=graph, tests=[])
    builder.go()
    rc = builder.exit_code
    if rc != 0:
        _report_and_exit(app_ctx, "Build failed")


def _reduced_ya_make_options():
    import devtools.ya.core.yarg.options
    import devtools.ya.core.common_opts
    import devtools.ya.test.opts

    build_graph_cache_opts = build_opts.build_graph_cache_config_opts()
    checkout_opts = build_opts.svn_checkout_options()

    useless = {
        build_opts.ContinueOnFailOptions,
        build_opts.CreateSymlinksOptions,
        build_opts.CustomGraphAndContextOptions,
        build_opts.DefaultNodeRequirementsOptions,
        build_opts.DumpReportOptions,
        build_opts.ExecutorOptions,
        build_opts.ForceDependsOptions,
        build_opts.GenerateLegacyDirOptions,
        build_opts.IgnoreNodesExitCode,
        build_opts.InstallDirOptions,
        build_opts.JavaSpecificOptions,
        build_opts.MDSUploadOptions,
        build_opts.OutputOptions,
        build_opts.SandboxUploadOptions,
        build_opts.SonarOptions,
        build_opts.StreamReportOptions,
        build_opts.TestenvReportDirOptions,
        build_opts.YaMakeOptions,
        build_opts.YndexerOptions,
        devtools.ya.core.common_opts.CommonUploadOptions,
        devtools.ya.core.common_opts.MiniYaOpts,
        devtools.ya.core.common_opts.PrintStatisticsOptions,
        devtools.ya.core.common_opts.ProfileOptions,
        devtools.ya.core.common_opts.ProfilerOptions,
        devtools.ya.core.common_opts.TeamcityOptions,
        devtools.ya.core.yarg.options.RawParamsOptions,
        devtools.ya.test.opts.CanonizationOptions,
        devtools.ya.test.opts.ConsoleReportOptions,
        devtools.ya.test.opts.CoverageOptions,
        devtools.ya.test.opts.DebuggingOptions,
        devtools.ya.test.opts.DepsOptions,
        devtools.ya.test.opts.DistbuildOptions,
        devtools.ya.test.opts.FileReportsOptions,
        devtools.ya.test.opts.FilteringOptions,
        devtools.ya.test.opts.FuzzOptions,
        devtools.ya.test.opts.HermioneOptions,
        devtools.ya.test.opts.InterimOptions,
        devtools.ya.test.opts.InternalDebugOptions,
        devtools.ya.test.opts.JavaOptions,
        devtools.ya.test.opts.JUnitOptions,
        devtools.ya.test.opts.LintersOptions,
        devtools.ya.test.opts.ListingOptions,
        devtools.ya.test.opts.OutputOptions,
        devtools.ya.test.opts.PytestOptions,
        devtools.ya.test.opts.RunTestOptions,
        devtools.ya.test.opts.RuntimeEnvironOptions,
        devtools.ya.test.opts.TestsOverSandboxOptions,
        devtools.ya.test.opts.TestsOverYtOptions,
        devtools.ya.test.opts.TestToolOptions,
        devtools.ya.test.opts.UidCalculationOptions,
    }

    # FOR OPENSOURCE
    useless |= {opt.__class__ for opt in build_graph_cache_opts}
    useless |= {opt.__class__ for opt in checkout_opts}

    useful = []
    for opt in build_opts.ya_make_options(free_build_targets=True, build_type="release"):
        if opt.__class__ not in useless:
            useful.append(opt)

    return useful


def _report_and_exit(app_ctx, message):
    app_ctx.display.emit_message(message)
    sys.exit(1)
