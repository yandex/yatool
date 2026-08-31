import json
import os
import sys
import logging
import typing

from collections import defaultdict

from devtools.ya.core.yarg import (
    ArgConsumer,
    EnvConsumer,
    SetConstValueHook,
    SetValueHook,
    Options,
    OptsHandler,
    FreeArgConsumer,
    ConfigConsumer,
    SetAppendHook,
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
from devtools.ya.core.yarg.handler import print_formatted, SimpleHandler
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
Check the tool's path in build/ya.conf.json or build/tools/tools/{tool_name}.tool.json since there is mismatch in real and supposed paths.
Please contact owners of the tool to fix that issue."""

TOOL_TIER_HEADERS = {
    tools.TOOL_TIER_OFFICIAL: "OFFICIAL - have owners, supported and actively developed",
    tools.TOOL_TIER_COMMUNITY: "COMMUNITY - have owners, but no reliable support",
    tools.TOOL_TIER_UNSUPPORTED: "UNSUPPORTED - useful tools without explicit owners. Use at your own risk",
    tools.TOOL_TIER_INFRASTRUCTURE: "INFRASTRUCTURE - supported by DEVTOOLS",
    tools.TOOL_TIER_DEPRECATED: "DEPRECATED - are subject to remove. Don't use",
    tools.TOOL_TIER_UNSPECIFIED: "UNSPECIFIED - no information about tier",
}


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
        self._cached_sub_handlers = None

        legacy_options = get_legacy_options()
        actual_options = [
            YaToolOptions(),
            ShowHelpOptions(raise_exception=False),
            CustomFetcherOptions(),
            UniversalFetcherOptions(),
            ToolsOptions(),
        ]
        free_args_options = [FreeArgsOption()]

        # It's important that full_option_list reuses the same Options() objects as legacy_options
        self._opt = merge_opts(actual_options + legacy_options + free_args_options)
        self._completion_opt = merge_opts(actual_options + legacy_options)
        self._legacy_opt = merge_opts(legacy_options + free_args_options)
        self._examples = [
            UsageExample("{prefix}", "Show this help and tool list"),
            UsageExample("{prefix} --list", "List official tools"),
            UsageExample("{prefix} --list --json", "List official tools in JSON format"),
            UsageExample("{prefix} --list --show-all", "List all tools"),
            UsageExample("{prefix} --card <tool>", "Show tool details"),
            UsageExample("{prefix} --card --json <tool>", "Show tool details in JSON format"),
            UsageExample("{prefix} --card --with-path", "Show tool details with tool paths"),
            UsageExample("{prefix} --print-path <tool>", "Print path to tool executable file"),
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
            # We disable an exception in ShowHelpOptions because we want the show_all and flat_list parameters to be read from configs
            if params.help_exception is not None:
                raise params.help_exception
            if not (params.args or params.list):
                raise ShowHelpException()
        except ShowHelpException as exc:
            OptsHandler.register_handler_run(prefix, args)
            usage = self.description + "\n\n"
            usage += self.format_usage(prefix) + "\n\n"
            usage += format_examples(self.opts_recursive(tuple(prefix)))
            usage += "\n" + self._format_help(exc.help_level, exc.help_search)
            usage += "\n\nAvailable tools:\n" + _get_tool_list(
                None, show_all=params.show_all, flat_list=params.flat_list
            )
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

        tool = None
        name_parts = []
        while params.args:
            name, params.args = params.args[0], params.args[1:]
            if name.startswith("-"):
                raise ArgsValidatingException("Can't handle arg: {}. Tool name is expected".format(name))
            name_parts.append(name)
            try:
                tool = self._get_tool(name_parts, params)
            except tools.ToolNotFoundException:
                if len(name_parts) > 1:
                    raise
                # try to guess first name part
                name_parts[0] = _guess_tool_name(name_parts[0])
                tool = self._get_tool(name_parts, params)
            if tool.config.type != tools.TOOL_TYPE_PARENT:
                break

        additional_handler_info = {
            "tool_name": name_parts,
            "tool_args": params.args,
        }
        OptsHandler.register_handler_run(prefix, args, additional_handler_info=additional_handler_info)

        params.tool = tool
        return self._action(params)

    @property
    def options(self) -> Options:
        return self._completion_opt

    @property
    def sub_handlers(self):
        if self._cached_sub_handlers is not None:
            return self._cached_sub_handlers
        self._cached_sub_handlers = self._recursive_sub_handlers()
        return self._cached_sub_handlers

    def format_usage(self, prefix: list[str] | tuple[str, ...]) -> str:
        return "[[imp]]Usage[[rst]]:\n  " + " ".join(prefix) + " [OPTIONS]... [tool_name [--] [TOOL OPTIONS]...]"

    def opts_recursive(self, prefix: tuple[str, ...]) -> dict[tuple[str, ...], list[UsageExample]]:
        return {prefix: self._examples}

    def _format_help(self, help_level: int, search_query: str | None) -> str:
        return format_help(self._opt, help_level, search_query=search_query)

    def _get_tool(self, name_parts: list[str], params: Params) -> tools._XTool:
        # To be compatible with the old code
        if params.need_resource_id or params.print_toolchain_path:
            for_platform = params.platform or params.host_platform or None
        else:
            for_platform = params.host_platform or None
        return tools.xtool(
            name_parts,
            toolchain_extra=params.toolchain,
            for_platform=for_platform,
            target_platform=params.target_platform,
            force_refetch=params.force_refetch,
        )

    def _recursive_sub_handlers(self, parent: tuple[str] | None = None) -> dict[str, SimpleHandler] | None:
        parent = parent or ()
        sub_handlers = {}
        for tool_cfg in tools.tools(parent):
            if tool_cfg.type == tools.TOOL_TYPE_PARENT:
                nested_sub_handlers = self._recursive_sub_handlers(tool_cfg.name_parts)
            else:
                nested_sub_handlers = None
            sub_handlers[tool_cfg.name_parts[-1]] = _FakeToolHandler(nested_sub_handlers)
        return sub_handlers


class _FakeToolHandler(SimpleHandler):
    def __init__(self, sub_handlers: dict[str, SimpleHandler] | None):
        self._sub_handlers = sub_handlers

    @property
    def sub_handlers(self) -> dict[str, SimpleHandler] | None:
        return self._sub_handlers

    @property
    def options(self) -> Options:
        return merge_opts([])


# All new ya tool options must be added here
# Eventually all options should migrate from LegacyYaToolOptions to this class
class YaToolOptions(Options):
    def __init__(self) -> None:
        super().__init__()
        self.tool = None  # Set by YaToolHandler.handle
        self.list = False
        self.json = False
        self.flat_list = True
        self.show_all = True
        self.card = False
        self.with_path = False
        self.show_tool_options_warning = False
        self.tags = []

    @staticmethod
    def consumer() -> list[ArgConsumer | EnvConsumer | ConfigConsumer]:
        return [
            ArgConsumer(["--disable-fastpath"], help="Always run python ya tool version", hook=NoValueDummyHook()),
            ArgConsumer(
                ["--json"],
                help="Dump tools info in JSON format",
                hook=SetConstValueHook("json", True),
            ),
            ArgConsumer(
                ["--list"],
                help="Hide ya tool help and show available tools only",
                hook=SetConstValueHook("list", True),
            ),
            ArgConsumer(
                ["--show-all"],
                help="Show all tools, including deprecated ones",
                hook=SetConstValueHook("show_all", True),
            ),
            ArgConsumer(
                ["--tag"],
                help="Show tools with specified tag only. Example: '--tag t1,t2 --tag t3' - show tool with both 't1' and 't2' tags or tag 't3'",
                hook=SetAppendHook("tags"),
            ),
            ConfigConsumer("show_all"),
            EnvConsumer("YA_TOOL_SHOW_ALL", hook=SetValueHook("show_all", transform=strtobool)),
            ArgConsumer(
                ["--flat-list"],
                help="Dont't group tool list by tiers",
                hook=SetConstValueHook("flat_list", True),
            ),
            ConfigConsumer("flat_list"),
            EnvConsumer("YA_TOOL_FLAT_LIST", hook=SetValueHook("flat_list", transform=strtobool)),
            ArgConsumer(
                ["--card"],
                help="Show detailed tool information",
                hook=SetConstValueHook("card", True),
            ),
            ArgConsumer(
                ["--with-path]"],
                help="Add paths to --card output. Note: triggers tool fetching",
                hook=SetConstValueHook("with_path", True),
            ),
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


def _useful_env_vars() -> dict[str, str]:
    return {'YA_TOOL': sys.argv[0]}


def do_tool(params: Params) -> None:
    tool = params.tool

    if params.card:
        if tool is None:
            raise ArgsValidatingException("--card requires tool name")
        print(_get_tool_card(tool, params))
        return

    if tool is None or tool.config.type == tools.TOOL_TYPE_PARENT:
        parent_parts = tool.config.name_parts if tool is not None else None
        print(
            _get_tool_list(
                parent_parts,
                show_all=params.show_all,
                tags=params.tags,
                flat_list=params.flat_list,
                as_json=params.json,
            )
        )
        return

    extra_args = params.args

    # The executable() method starts tool fetching
    # Do it in an async thread to allow break the program with Ctrl-C
    tool_path = exts.asyncthread.future(tool.executable)()

    if windows.on_win() and not tool_path.endswith('.exe'):  # XXX: hack. Think about ya.conf.json format
        logger.debug('Rename tool for win: %s', tool_path)
        tool_path += '.exe'

    lock_result = False

    if params.print_toolchain_path:
        print(tool.toolchain_root())
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
        for key, value in tool.environ().items():
            env[key] = os.pathsep.join(value)
        if tool.name == 'gdb':
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
            tool.name == 'arc'
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
                'tool_name': tool.name,
                'tool_path': tool_path,
                'extra_args': extra_args,
            },
        )
        exts.process.execve(tool_path, extra_args, env=env)
    else:
        raise ArgsValidatingException(
            TOOL_REAL_AND_SUPPOSED_PATHS_ARE_MISSMATCHED_MSG.format(tool_name=tool.name, tool_path=tool_path)
        )

    if lock_result:
        lock_resource(tool.toolchain_root())


def _get_tool_flat_list(tool_cfg_list: list[tools._ToolConfig], max_name_len: int) -> list[str]:
    result = []
    for tool_cfg in tool_cfg_list:
        desc_items = tool_cfg.description.split('\n')
        if tool_cfg.tier.tier == tools.TOOL_TIER_DEPRECATED:
            desc_items.append("DEPRECATED: {}".format(tool_cfg.tier.deprecation_cause))
        result += _get_aligned_value(tool_cfg.name, desc_items, max_name_len + 5, prefix="  ")
    return result


def _get_tool_list(
    parent_parts: tuple[str, ...] | None,
    show_all: bool = False,
    tags: list[str] | None = None,
    flat_list: bool = False,
    as_json: bool = False,
) -> str:
    tool_cfg_list = tools.tools(parent_parts)
    total_tool_count = len(tool_cfg_list)
    # Don't apply filters to child tools or if a full tool list is requested
    if not parent_parts and not show_all:
        # Note: if tiers are disabled (for example, in Open Source) all tools have an 'unspecified' tier
        tool_cfg_list = [
            t for t in tool_cfg_list if t.tier.tier in (tools.TOOL_TIER_OFFICIAL, tools.TOOL_TIER_UNSPECIFIED)
        ]
    if tags:
        # Apply tag filter
        tag_filters = []
        for t in tags:
            if t := set(y for y in (x.strip() for x in t.split(",")) if y):
                tag_filters.append(t)

        filtered_cfg_list = []
        for tool_cfg in tool_cfg_list:
            tool_tags = set(tool_cfg.tags or [])
            if tool_tags and any(f <= tool_tags for f in tag_filters):
                filtered_cfg_list.append(tool_cfg)
        tool_cfg_list = filtered_cfg_list

    filtered_tool_count = len(tool_cfg_list)

    tool_cfg_list.sort(key=lambda t: t.name)

    if as_json:
        result = []
        for tool_cfg in tool_cfg_list:
            tier_info = tool_cfg.tier
            item = {
                "name": tool_cfg.name,
                "tier": tier_info.tier,
                "is_parent": tool_cfg.type == tools.TOOL_TYPE_PARENT,
                "description": tool_cfg.description,
            }
            if tier_info.tier == tools.TOOL_TIER_DEPRECATED:
                item["deprecation_cause"] = tier_info.deprecation_cause
            result.append(item)
        return json.dumps(result, indent=4)

    # It's safe to set max_name_len to zero if tool_cfg_list is empty
    max_name_len = max((len(x.name) for x in tool_cfg_list)) if tool_cfg_list else 0
    if parent_parts or flat_list:
        result = _get_tool_flat_list(tool_cfg_list, max_name_len)
    else:
        result = []
        grouped_tools = defaultdict(list)
        for tool_cfg in tool_cfg_list:
            grouped_tools[tool_cfg.tier.tier].append(tool_cfg)
        for tier, header in TOOL_TIER_HEADERS.items():
            if cfgs := grouped_tools.get(tier):
                result.append(header + ":")
                result += _get_tool_flat_list(cfgs, max_name_len)
                result.append("")

    if filtered_tool_count < total_tool_count:
        result.append("")
        result.append(
            "Showing {} official tools out of {} total. Use --show-all to see the full list.".format(
                filtered_tool_count, total_tool_count
            )
        )

    return "\n".join(result)


def _get_tool_card(tool: tools._XTool, params: Params) -> str:
    SIMPLE_ATTRS = ("owners", "docs", "source", "examples", "releases", "tags", "skill", "host_platforms")

    tool_card = {
        "name": tool.name,
        "description": tool.config.description,
        "tier": tool.config.tier.tier,
    }
    if tool.config.availability != tools.TOOL_AVAILABILITY_FULL:
        tool_card["description"] = "[HIDDEN - NOT FOR PUBLIC USE] " + tool_card["description"]
    _add_if_not_empty(tool_card, "tier", tool.config.tier.tier)
    if tool.config.tier.tier == tools.TOOL_TIER_DEPRECATED:
        tool_card["deprecation_cause"] = tool.config.tier.deprecation_cause
    _add_if_not_empty(tool_card, "revised", tool.config.tier.revised)
    if support := tool.config.support:
        support_card = {k: v for k, v in support.__dict__.items() if v}
        _add_if_not_empty(tool_card, "support", support_card)
    for attr in SIMPLE_ATTRS:
        _add_if_not_empty(tool_card, attr, getattr(tool.config, attr))
    is_parent = tool.config.type == tools.TOOL_TYPE_PARENT
    if is_parent:
        tool_card["is_parent"] = True
    elif params.with_path:
        tool_card["executable"] = tool.executable()
        tool_card["toolchain_root"] = tool.toolchain_root()
        tool_card["resource_url"] = tool.resource_url()

    if params.json:
        return json.dumps(tool_card, indent=4)

    result = []
    description_lines = tool_card["description"].split("\n")
    header = "{} - {}{}{}".format(
        tool_card["name"],
        description_lines[0],
        " [PARENT]" if is_parent else "",
        (
            " [DEPRECATED: {}]".format(tool_card["deprecation_cause"])
            if tool_card["tier"] == tools.TOOL_TIER_DEPRECATED
            else ""
        ),
    )
    result.append(header)
    result.append("Config:")
    if len(description_lines) > 1:
        result += _get_text_card_value("description", description_lines)
    for attr in ("host_platforms", "tier", "revised", "owners"):
        result += _get_text_card_value(attr, tool_card.get(attr))
    if support := tool_card.get("support"):
        support_lines = sum([_get_aligned_value(k + ":", v, indent=12) for k, v in support.items()], [])
        result += _get_text_card_value("support", support_lines)
    for attr in ("source", "releases", "docs", "skill"):
        result += _get_text_card_value(attr, tool_card.get(attr))
    result += _get_text_card_value("tags", ", ".join(tool_card.get("tags", [])))
    if examples := tool_card.get("examples"):
        result.append("")
        result.append("Examples:")
        result += _get_aligned_value("", examples, indent=0, prefix="  ")
    if not is_parent and params.with_path:
        result.append("")
        result.append("Paths:")
        for attr in ("executable", "toolchain_root", "resource_url"):
            result += _get_text_card_value(attr, tool_card.get(attr))

    return "\n".join(result)


def _add_if_not_empty(dict: dict[str, typing.Any], key: str, value: typing.Any) -> bool:
    if value:
        dict[key] = value
        return True
    return False


def _get_text_card_value(key: str, value: str | list[str] | None) -> list[str]:
    return _get_aligned_value(key, value, 17, prefix="  ", key_suffix=" - ")


def _get_aligned_value(
    key: str,
    value: str | list[str] | None,
    indent: int,
    prefix: str = "",
    key_suffix: str = "",
) -> list[str]:
    if not value:
        return []
    if isinstance(value, str):
        value = [value]
    result = []
    if key_suffix:
        key = "{key:{indent}}{key_suffix}".format(key=key, indent=indent - len(key_suffix), key_suffix=key_suffix)
    for line in value:
        result.append("{prefix}{key:{indent}}{line}".format(prefix=prefix, key=key, indent=indent, line=line))
        key = ""
    return result


def _guess_tool_name(orig_tool_name):
    all_tool_names = sorted(t.name for t in tools.tools(visible_only=False))
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
