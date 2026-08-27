import os
import sys
import logging
import typing

from devtools.ya.core.yarg import (
    ArgConsumer,
    EnvConsumer,
    SetConstValueHook,
    SetValueHook,
    Options,
    OptsHandler,
    FreeArgConsumer,
    ConfigConsumer,
    ShowHelpException,
    NoValueDummyHook,
    UsageExample,
    ArgsValidatingException,
    ShowHelpOptions,
    BaseHandler,
    Params,
    merge_opts,
)

import devtools.ya.app

from devtools.ya.build.build_opts import CustomFetcherOptions, SandboxAuthOptions, ToolsOptions, UniversalFetcherOptions
from devtools.ya.core.yarg.help import format_help, format_examples
from devtools.ya.core.yarg.handler import print_formatted
from yalibrary import tools
from yalibrary.toolscache import lock_resource
import devtools.ya.core.config
import devtools.ya.core.respawn
import exts.process
from exts.strtobool import strtobool
from library.python import windows
import exts.asyncthread

logger = logging.getLogger(__name__)

TOOL_REAL_AND_SUPPOSED_PATHS_ARE_MISSMATCHED_MSG = """Executable for tool `{tool_name}` is not found at {tool_path}.
You can run ya tool {tool_name} --print-path to check where tool is located on FS.
Check the tool's path in build/ya.conf.json since there is mismatch in real and supposed paths.
Please contact owners of the tool to fix that issue."""


def get_legacy_options():
    return [
        LegacyYaToolOptions(),
        SandboxAuthOptions(),
    ]


class ToolYaHandler(BaseHandler):
    description = 'Execute specific tool'

    def __init__(self) -> None:
        super().__init__()
        self._action = devtools.ya.app.execute(action=do_tool, respawn=devtools.ya.app.RespawnType.OPTIONAL)

        legacy_options = get_legacy_options()
        actual_options = [
            YaToolOptions(),
            ShowHelpOptions(),
            CustomFetcherOptions(),
            UniversalFetcherOptions(),
            ToolsOptions(),
        ]
        free_args_options = [FreeArgsOption()]

        # It's important that full_option_list reuses the same Options() objects as legacy_options
        self._opt = merge_opts(actual_options + legacy_options + free_args_options)
        self._legacy_opt = merge_opts(legacy_options + free_args_options)
        self._examples = [
            UsageExample("{prefix}", "Show this help and tool list"),
            UsageExample("{prefix} --print-path <tool>", "Print path to the tool executable file"),
            UsageExample(
                "{prefix} --force-update <tool> [TOOL OPTIONS]",
                "Check tool for updates before the update interval elapses",
            ),
        ]

    def handle(self, root_handler: BaseHandler, args: list[str], prefix: list[str]) -> typing.Any:
        params = None
        try:
            params = self._opt.initialize(
                args,
                prefix=prefix,
                stop_at_first_unknown_arg=True,
                user_config=strtobool(os.getenv("YA_LOAD_USER_CONF", "1")),
            )
            if not params.args:
                raise ShowHelpException()
        except ShowHelpException as exc:
            OptsHandler.register_handler_run(prefix, args)
            usage = self.description + "\n\n"
            usage += self.format_usage(prefix) + "\n\n"
            usage += format_examples(self.opts_recursive(tuple(prefix)))
            usage += "\n" + self._format_help(exc.help_level, exc.help_search)
            usage += "\n\nAvailable tools:\n" + _get_tool_list()
            print_formatted(usage)
            sys.exit(0)

        old_free_args = params.args.copy()

        # Here is a tricky part: legacy_opt.initialize() updates the same option objects as the self._opt.initialize() did.
        # This combines the effect of both options locations: before the tool name and after it. Free args are rewritten.
        self._legacy_opt.initialize(
            old_free_args,
            prefix=prefix,
            unknown_args_as_free=True,
            user_config=strtobool(os.getenv("YA_LOAD_USER_CONF", "1")),
        )
        # Get updated values from the options
        params = self._opt.params()

        if old_free_args != params.args and sys.stderr.isatty() and params.show_tool_options_warning:
            sys.stderr.write(
                "WARNING: specify internal ya tool options before the tool name: 'ya tool <ya options>... <tool name> <tool options>...\n"
            )

        assert len(params.args) > 0, "Legacy options somehow are superset of full options (bug?)"
        tool_name, params.args = params.args[0], params.args[1:]
        if tool_name.startswith("-"):
            raise ArgsValidatingException("Can't handle arg: {}. Tool name is expected".format(tool_name))

        tool_name = _guess_tool_name(tool_name)

        additional_handler_info = {
            "tool_name": [tool_name],
            "tool_args": params.args,
        }
        OptsHandler.register_handler_run(prefix, args, additional_handler_info=additional_handler_info)

        params.tool = tool_name
        return self._action(params)

    @property
    def options(self) -> Options:
        return self._opt

    def format_usage(self, prefix: list[str] | tuple[str, ...]) -> str:
        return "[[imp]]Usage[[rst]]:\n  " + " ".join(prefix) + " [OPTIONS]... [tool_name [--] [TOOL OPTIONS]...]"

    def opts_recursive(self, prefix: tuple[str, ...]) -> dict[tuple[str, ...], list[UsageExample]]:
        return {prefix: self._examples}

    def _format_help(self, help_level: int, search_query: str | None) -> str:
        return format_help(self._opt, help_level, search_query=search_query)


# All new ya tool options must be added here
# Eventually all options should migrate from LegacyYaToolOptions to this class
class YaToolOptions(Options):
    def __init__(self) -> None:
        super().__init__()
        self.tool = None  # Set by YaToolHandler.handle
        self.show_tool_options_warning = False

    @staticmethod
    def consumer() -> list[ArgConsumer | EnvConsumer | ConfigConsumer]:
        return [
            ArgConsumer(["--disable-fastpath"], help="Always run python ya tool version", hook=NoValueDummyHook()),
            ConfigConsumer("show_tool_options_warning"),
            EnvConsumer(
                "YA_SHOW_TOOL_OPTIONS_WARNING", hook=SetValueHook("show_tool_options_warning", transform=strtobool)
            ),
        ]

    def postprocess(self) -> None:
        super().postprocess()


class FreeArgsOption(Options):
    def __init__(self) -> None:
        super().__init__()
        self.args = []

    @staticmethod
    def consumer() -> list[FreeArgConsumer]:
        return [
            FreeArgConsumer(hook=SetValueHook(name="args")),
        ]


# As soon an option is no longer used after the tool name move it to the YaToolOption
class LegacyYaToolOptions(Options):
    def __init__(self) -> None:
        super().__init__()
        self.print_path = None
        self.print_toolchain_path = None
        self.toolchain = None
        self.platform = None
        self.target_platform = None
        self.need_resource_id = None
        self.show_help = False
        self.host_platform = None
        self.force_update = False
        self.force_refetch = False

    @staticmethod
    def consumer() -> list[ArgConsumer | EnvConsumer | ConfigConsumer]:
        return [
            ArgConsumer(
                ['--print-path'],
                help='Only print path to tool, do not execute',
                hook=SetConstValueHook('print_path', True),
            ),
            ArgConsumer(
                ['--print-toolchain-path'],
                help='Print path to toolchain root',
                hook=SetConstValueHook('print_toolchain_path', True),
            ),
            ArgConsumer(
                ['--platform'],
                help="Set specific platform. DEPRECATED: use --host-platform instead",
                hook=SetValueHook('platform'),
            ),
            ArgConsumer(['--host-platform'], help="Set host platform", hook=SetValueHook('host_platform')),
            EnvConsumer('YA_TOOL_HOST_PLATFORM', hook=SetValueHook('host_platform')),
            ArgConsumer(['--toolchain'], help="Specify toolchain", hook=SetValueHook('toolchain')),
            ArgConsumer(
                ['--get-resource-id'],
                help="Get resource id for specific platform (the platform should be specified)",
                hook=SetConstValueHook('need_resource_id', True),
            ),
            # Don't move to actual YaToolOptions. This option will die with LegacyYaToolOptions
            ArgConsumer(
                ['--ya-help'], help="Show help (deprecated)", visible=False, hook=SetConstValueHook('show_help', True)
            ),
            ArgConsumer(
                ['--target-platform'],
                help='Target platform',
                hook=SetValueHook('target_platform', transform=lambda x: x.upper()),
            ),
            # Don't move to actual YaToolOptions. This option will die with LegacyYaToolOptions
            ArgConsumer(
                ['--hide-arm64-host-warning'],
                help='Hide MacOS arm64 host warning (deprecated, no op)',
                hook=NoValueDummyHook(),
                visible=True,
            ),
            ArgConsumer(
                ['--force-update'],
                help='Check tool for updates before the update interval elapses',
                hook=SetConstValueHook('force_update', True),
            ),
            ArgConsumer(['--force-refetch'], help='Refetch toolchain', hook=SetConstValueHook('force_refetch', True)),
            ArgConsumer(
                ["--no-fallback-to-python"],
                help="Don't return to python if fast-path failed",
                hook=NoValueDummyHook(),
                visible=False,
            ),
            ArgConsumer(
                ["--print-fastpath-error"], help="Print fast path failure error", hook=NoValueDummyHook(), visible=False
            ),
        ]

    def postprocess(self) -> None:
        if self.show_help:
            raise ShowHelpException()
        if self.toolchain and self.target_platform:
            raise ArgsValidatingException("Do not use --toolchain and --target-platform args together")
        if self.force_update:
            os.environ['YA_TOOL_FORCE_UPDATE'] = "1"


def _replace(s: str, transformations: dict[str, str]) -> str:
    for k, v in transformations.items():
        s = s.replace('$({})'.format(k), v)
    return s


def _useful_env_vars() -> dict[str, str]:
    return {'YA_TOOL': sys.argv[0]}


def do_tool(params: Params) -> None:
    tool_name = params.tool
    extra_args = params.args
    target_platform = params.target_platform
    host_platform = params.host_platform

    for_platform = params.platform or params.host_platform or None

    if params.need_resource_id:
        print(tools.resource_id(tool_name, params.toolchain, for_platform))
        return

    tool_getter = exts.asyncthread.future(
        lambda: tools.tool(
            tool_name,
            params.toolchain,
            target_platform=target_platform,
            for_platform=host_platform,
            force_refetch=params.force_refetch,
        )
    )
    tool_path = tool_getter()

    if windows.on_win() and not tool_path.endswith('.exe'):  # XXX: hack. Think about ya.conf.json format
        logger.debug('Rename tool for win: %s', tool_path)
        tool_path += '.exe'

    lock_result = False

    if params.print_toolchain_path:
        print(tools.toolchain_root(tool_name, params.toolchain, for_platform))
        lock_result = True
    elif params.print_path:
        print(tool_path)
        lock_result = True
    elif os.path.isfile(tool_path):
        env = devtools.ya.core.respawn.filter_env(os.environ.copy())

        # Remove environment variables set by 'ya' wrapper.
        # They are actually one-time ya-bin parameters rather than inheritable environment
        # for all descendant processes.
        for key in ('YA_SOURCE_ROOT',):
            env.pop(key, None)

        env.update(_useful_env_vars())
        for key, value in tools.environ(tool_name, params.toolchain).items():
            env[key] = _replace(
                os.pathsep.join(value), {'ROOT': tools.toolchain_root(tool_name, params.toolchain, for_platform)}
            )
        if tool_name == 'gdb':
            # gdb does not fit in 8 MB stack with large cores (DEVTOOLS-5040).
            try:
                import resource as r
            except ImportError:
                pass
            else:
                soft, hard = r.getrlimit(r.RLIMIT_STACK)
                new = 128 << 20
                logger.debug("Limit info: soft=%d hard=%d new=%d", soft, hard, new)
                if hard != r.RLIM_INFINITY:
                    new = min(new, hard)
                    logger.debug("Limit info: new=%d", new)
                if new > soft:
                    logger.debug("Limit info: setting new limits=(%d, %d)", new, hard)
                    try:
                        r.setrlimit(r.RLIMIT_STACK, (new, hard))
                    except ValueError as e:
                        logger.error("Failure while setting RLIMIT_STACK ({}, {}), {}".format(new, hard, e))
                        logger.exception("While setting RLIMIT_STACK")
            arc_root = os.environ.get('YA_TOOL_GDB_ARCADIA_ROOT', None)
            if arc_root is None:
                arc_root = devtools.ya.core.config.find_root(fail_on_error=False)
            if arc_root:
                logger.debug('Arcadia root: [%s]', arc_root)
                extra_args = ['-ex', 'set substitute-path /-S/ {}/'.format(arc_root)] + extra_args
                extra_args = ['-ex', 'set filename-display absolute'] + extra_args
        if (
            tool_name == 'arc'
            and params.username not in {'sandbox', 'root'}
            and os.getenv('YA_ALLOW_TOOL_ARC', 'no') != 'yes'
        ):
            message = (
                'Please, use natively installed arc, install guide:'
                ' https://docs.yandex-team.ru/devtools/intro/quick-start-guide#arc-setup'
            )
            raise ArgsValidatingException(message)
        from devtools.ya.core.report import telemetry, ReportTypes

        telemetry.report(
            ReportTypes.TOOL_EXECUTION,
            {
                'tool_launch_method': 'python_tool_launcher',
                'tool_name': tool_name,
                'tool_path': tool_path,
                'extra_args': extra_args,
            },
        )
        exts.process.execve(tool_path, extra_args, env=env)
    else:
        raise ArgsValidatingException(
            TOOL_REAL_AND_SUPPOSED_PATHS_ARE_MISSMATCHED_MSG.format(tool_name=tool_name, tool_path=tool_path)
        )

    if lock_result:
        lock_resource(tools.toolchain_root(tool_name, params.toolchain, for_platform))


def _get_tool_list() -> str:
    tool_info_list = sorted([t for t in tools.tools() if t.visible], key=lambda t: t.name)

    if not tool_info_list:
        return ""

    result = []
    max_name_len = max((len(x.name) for x in tool_info_list))
    for tool_info in tool_info_list:
        desc_items = tool_info.description.split('\n')
        result += _get_aligned_value(tool_info.name, desc_items, max_name_len + 5, prefix="  ")
    return "\n".join(result)


def _get_aligned_value(
    key: str,
    value: str | list[str] | None,
    indent: int,
    prefix: str = "",
) -> list[str]:
    if not value:
        return []
    if isinstance(value, str):
        value = [value]
    result = []
    for line in value:
        result.append("{prefix}{key:{indent}}{line}".format(prefix=prefix, key=key, indent=indent, line=line))
        key = ""
    return result


def _guess_tool_name(orig_tool_name):
    all_tool_names = sorted(t.name for t in tools.tools())
    if orig_tool_name in all_tool_names:
        return orig_tool_name

    import pylev

    result = orig_tool_name
    for tool_name in all_tool_names:
        new_result = None
        if tool_name.startswith(orig_tool_name):
            new_result = tool_name
        elif len(orig_tool_name) > 2:
            l_dist = pylev.damerau_levenshtein(orig_tool_name, tool_name)
            if l_dist < 2:
                new_result = tool_name
        if new_result:
            # too many similar handlers
            if result != orig_tool_name:
                return orig_tool_name

            result = new_result

    return result
