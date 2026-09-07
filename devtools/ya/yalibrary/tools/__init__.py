# coding=utf-8
import copy
import enum
import json
import logging
import os
import typing
from collections import defaultdict

import devtools.ya.core.config
import yalibrary.fetcher.tool_chain_fetcher
import yalibrary.platform_matcher as pm
import exts.path2
import devtools.libs.yaplatform.python.platform_map as platform_map
import yalibrary.toolscache as toolscache

from typing import NotRequired

logger = logging.getLogger(__name__)


class ToolType(enum.StrEnum):
    SIMPLE = enum.auto()
    TOOLCHAIN = enum.auto()
    PARENT = enum.auto()


class ToolAvailability(enum.StrEnum):
    FULL = enum.auto()
    HIDDEN = enum.auto()
    INTERNAL = enum.auto()


class ToolTier(enum.StrEnum):
    OFFICIAL = enum.auto()
    COMMUNITY = enum.auto()
    UNSUPPORTED = enum.auto()
    INFRASTRUCTURE = enum.auto()
    DEPRECATED = enum.auto()
    # For environments where tier system is not applicable.
    UNSPECIFIED = enum.auto()


_TOOLCHAIN_SEPARATOR = ','
_TOOL_NAME_SEPARATOR = " "
_DEFAULT_ARCH = "x86_64"


class ToolNotFoundException(Exception):
    mute = True


class ToolResolveException(Exception):
    mute = True


class UnsupportedPlatform(Exception):
    retriable = False
    mute = True


class UnsupportedToolchain(Exception):
    mute = True


class _Bottle:
    def __init__(
        self,
        toolchain_name: str,
        bottle_name: str,
        formula: dict[str, typing.Any],
        executable: str | dict[str, list[str]] | None,
        for_platform: str | None,
        force_refetch: bool,
    ) -> None:
        self.__formula = formula
        self.__bottle_name = bottle_name
        self.__executable = executable
        if self.__executable and not isinstance(self.__executable, dict):
            binname = self.__executable
        else:
            binname = None
        self.__fetcher = yalibrary.fetcher.tool_chain_fetcher.get_tool_chain_fetcher(
            devtools.ya.core.config.tool_root(toolscache.toolscache_version()),
            toolchain_name,
            bottle_name,
            self.__formula,
            for_platform,
            binname,
            force_refetch,
        )

    def resolve(self, cache: bool = True) -> str:
        return self.__fetcher.fetch_if_need(cache=cache).where

    def get_resource_id_from_cache(self) -> str:
        return self.__fetcher.resource_id_from_cache()

    def get_executable(self, name: str | None) -> str:
        if not self.__executable:
            return self.resolve()
        if isinstance(self.__executable, dict):
            # это tar архив с потенциально несколькими бинарниками
            if name not in self.__executable:
                raise Exception('Cannot find ' + name)
            suffix = self.__executable[name]
            path = self.resolve()
            return exts.path2.normpath(os.path.join(path, *suffix))
        else:
            # этот файл - сам является бинарником, это не архив!
            path = self.resolve()
            return exts.path2.normpath(os.path.join(path, self.__executable))


def tools(parent: typing.Iterable[str] | None = None, visible_only: bool = True) -> list['_ToolConfig']:
    parent_parts = tuple(parent) if parent is not None else None
    tool_cfg_list = _tool_config_reader.list_tools(parent_parts)
    if visible_only:
        return [t for t in tool_cfg_list if t.availability == ToolAvailability.FULL]
    else:
        return [t for t in tool_cfg_list if t.availability != ToolAvailability.INTERNAL]


def xtool(
    name: typing.Iterable[str],
    toolchain_extra: str | None = None,
    for_platform: str | None = None,
    target_platform: str | None = None,
    cache: bool = True,
    force_refetch: bool = False,
) -> '_XTool':
    tool = _XTool(
        tuple(name),
        toolchain_extra=toolchain_extra,
        for_platform=for_platform,
        target_platform=target_platform,
        cache=cache,
        force_refetch=force_refetch,
    )
    if tool.config.availability == ToolAvailability.INTERNAL:
        raise ToolResolveException("Tool {} is for internal use only".format(tool.name))
    return tool


# Extended tool class
class _XTool:
    def __init__(
        self,
        name_parts: tuple[str, ...],
        toolchain_extra: str | None = None,
        for_platform: str | None = None,
        target_platform: str | None = None,
        cache: bool = True,
        force_refetch: bool = False,
    ) -> None:
        self._for_platform = for_platform
        self._force_refetch = force_refetch
        self._cache = cache
        self._tool_cfg = _tool_config_reader.tool(name_parts)
        self._bottle_cache = None
        self._toolchain_root = None

        if self._tool_cfg.type != ToolType.PARENT:
            self._tc_name = self._get_best_toolchain_name(
                toolchain_extra=toolchain_extra, target_platform=target_platform
            )
            self._toolchain = self._tool_cfg.toolchains[self._tc_name]
            self._location = self._toolchain["tools"][self._tool_cfg.name]
        else:
            self._tc_name = None
            self._toolchain = None
            self._location = None

    @property
    def config(self) -> '_ToolConfig':
        return self._tool_cfg

    @property
    def name(self) -> str:
        return self.config.name

    def toolchain_root(self) -> str:
        assert self._tool_cfg.type != ToolType.PARENT, "Not applicable to parent tool"
        if self._toolchain_root is None:
            self._toolchain_root = self._bottle.resolve(cache=self._cache)
        return self._toolchain_root

    def executable(self) -> str:
        assert self._tool_cfg.type != ToolType.PARENT, "Not applicable to parent tool"
        executable_name = self._location.get('executable')
        if self._location.get("system"):
            return executable_name
        if not self._cache:
            # ensure bottle is resolved at least once
            self.toolchain_root()
        return self._bottle.get_executable(executable_name)  # if executable_name is None it's Ok

    def environ(self) -> dict[str, list[str]]:
        assert self._tool_cfg.type != ToolType.PARENT, "Not applicable to parent tool"
        environ = self._toolchain.get("env", {})
        if not environ:
            return {}
        # XXX Transformations are copied from old code (it may be outdated and require rethinking):
        # - the match_root variable is replaced by the unix-style toolchain path ('/' as a path separator)
        # - ROOT variable is replaced by the raw toolchain path (path separator is unknown)
        transformations = {
            "$(ROOT)": self.toolchain_root(),
        }
        match_root = self._toolchain.get("params", {}).get("match_root")
        if match_root is not None:
            transformations["$({0})".format(match_root.upper())] = exts.path2.normpath(self.toolchain_root())
        for var in environ.keys():
            environ[var] = [self._replace(x, transformations) for x in environ[var]]
        return environ

    def params(self) -> dict[str, typing.Any]:
        assert self._tool_cfg.type != ToolType.PARENT, "Not applicable to parent tool"
        params = copy.deepcopy(self._toolchain.get("params", {}))
        if self._location.get("system"):
            return params

        params["toolchain_root_path"] = exts.path2.normpath(self.toolchain_root())
        executable_name = self._location.get("executable")
        if executable_name is not None:
            params["toolchain_name"] = executable_name.upper()
        params["toolchain"] = self._tc_name
        return params

    def resource_url(self) -> str:
        assert self._tool_cfg.type != ToolType.PARENT, "Not applicable to parent tool"
        return str(self._bottle.get_resource_id_from_cache())

    @property
    def _bottle(self) -> _Bottle:
        if self._bottle_cache is None:
            bottle_name = self._location["bottle"]
            bottle_value = self._tool_cfg.bottles[bottle_name]
            self._bottle_cache = _Bottle(
                self._tc_name,
                bottle_name,
                bottle_value['formula'],
                bottle_value.get('executable'),
                self._for_platform,
                self._force_refetch,
            )
        return self._bottle_cache

    @staticmethod
    def _replace(s: str, transformations: dict[str, str]) -> str:
        for k, v in transformations.items():
            s = s.replace(k, v)
        return s

    def _get_best_toolchain_name(self, toolchain_extra: str | None = None, target_platform: str | None = None) -> str:
        current_os = pm.current_os()
        extra_tc = None
        if target_platform:
            if toolchain_extra:
                raise ToolResolveException("toolchain and target platform should not be specified together")
            extra_tc = _resolve_tool_by_host_os(self._tool_cfg.name, current_os, target_platform)['name']
        elif toolchain_extra:
            extra_tc = next((x for x in toolchain_extra.split(_TOOLCHAIN_SEPARATOR) if x), None)

        if extra_tc:
            if extra_tc in self._tool_cfg.toolchains:
                return extra_tc
            else:
                raise ToolNotFoundException('Cannot find toolchain: ' + extra_tc)

        best_tc_name = None
        is_native = False
        for tc_name, toolchain in self._tool_cfg.toolchains.items():
            for platform in toolchain["platforms"]:
                if not platform.get("default"):
                    continue

                if platform["host"]["os"] == current_os:
                    if "target" not in platform or platform["target"]["os"] == current_os:
                        return tc_name
                    elif not is_native:
                        is_native = True
                        best_tc_name = tc_name
                elif best_tc_name is None:
                    best_tc_name = tc_name

        if best_tc_name is None:
            raise ToolNotFoundException("Cannot find any default toolchain")

        return best_tc_name


def tool(
    name: str | typing.Iterable[str],
    toolchain_extra: str | None = None,
    with_params: bool = False,
    for_platform: str | None = None,
    target_platform: str | None = None,
    cache: bool = True,
    force_refetch: bool = False,
) -> str | tuple[str, dict[str, typing.Any]]:
    name_parts = _split_tool_name(name) if isinstance(name, str) else tuple(name)
    tool = _XTool(
        name_parts,
        toolchain_extra=toolchain_extra,
        for_platform=for_platform,
        target_platform=target_platform,
        cache=cache,
        force_refetch=force_refetch,
    )
    if with_params:
        return tool.executable(), tool.params()
    else:
        return tool.executable()


def resource_id(name: str | typing.Iterable[str], toolchain_extra: str | None, for_platform: str | None) -> str:
    name_parts = _split_tool_name(name) if isinstance(name, str) else tuple(name)
    return _XTool(name_parts, toolchain_extra=toolchain_extra, for_platform=for_platform).resource_url()


def toolchain_root(name: str | typing.Iterable[str], toolchain_extra: str | None, for_platform: str | None) -> str:
    name_parts = _split_tool_name(name) if isinstance(name, str) else tuple(name)
    return _XTool(name_parts, toolchain_extra=toolchain_extra, for_platform=for_platform).toolchain_root()


def toolchain_aliases() -> dict[str, str]:
    return _tool_config_reader.toolchain_aliases()


class ToolInfo(typing.TypedDict):
    platform: dict[str, typing.Any]
    env: dict[str, str]
    params: dict[str, str]
    formula: dict[str, str] | None
    name: str
    bottle_name: str
    executable_path: list[str]
    tool_var: NotRequired[str]


def _load_toolchain(
    toolchain_name: str,
    platforms: dict[str, typing.Any],
    platf_type: str,
    default_value: dict[str, typing.Any] | None = None,
) -> dict[str, typing.Any]:
    platform = platforms.get(platf_type, None)
    if not platform:
        if default_value:
            return copy.deepcopy(default_value)
        else:
            raise UnsupportedToolchain('%s platform should be always specified. %s', platf_type, platforms)
    else:
        res_os = platform.get('os', None)
        if not res_os:
            raise UnsupportedToolchain('OS should be defined. %s', platform)
        return {
            'os': res_os,
            'arch': platform.get('arch', _DEFAULT_ARCH),
            'toolchain': toolchain_name,
            'visible_name': toolchain_name,
        }


def _iter_platforms(descr: dict[str, typing.Any], toolchain_name: str) -> typing.Iterator[dict[str, typing.Any]]:
    for platforms in descr.get('platforms', []):
        host = _load_toolchain(toolchain_name, platforms, 'host')
        target = _load_toolchain(toolchain_name, platforms, 'target', host)

        yield {'host': host, 'target': target}

        if platforms.get('default', False):
            tc_def = {'toolchain': 'default'}
            h_copy = copy.deepcopy(host) | tc_def
            t_copy = copy.deepcopy(target) | tc_def
            yield {'host': h_copy, 'target': t_copy}


def _subst(x: typing.Any, root: str, tool_var: str) -> typing.Any:
    if isinstance(x, dict):
        return dict((_subst(k, root, tool_var), _subst(v, root, tool_var)) for k, v in x.items())

    if isinstance(x, list):
        return [_subst(v, root, tool_var) for v in x]
    if isinstance(x, str):
        if x == root:
            return tool_var

        return x.replace('$(' + root + ')', '$(' + tool_var + ')')

    return x


def iter_tools(
    name: str | typing.Iterable[str],
    tn_filter: typing.Callable[[str, dict[str, typing.Any]], bool] | None = None,
) -> typing.Iterator[ToolInfo]:
    name_parts = _split_tool_name(name) if isinstance(name, str) else tuple(name)
    tool_name = _join_tool_name(name_parts)
    try:
        tool_cfg = _tool_config_reader.tool(name_parts)
    except ToolNotFoundException:
        logger.debug("iter_tools() is called for not existing tool '{}'".format(tool_name))
        return

    toolchains = tool_cfg.toolchains
    bottles = tool_cfg.bottles

    for toolchain_key, descr in toolchains.items():
        if tn_filter is not None and not tn_filter(toolchain_key, descr):
            continue

        toolchain_name = descr.get('name', toolchain_key)
        tool = descr["tools"][tool_name]
        bottle_name = tool.get('bottle', None)
        executable_path = tool.get('executable', None)

        formula = bottles[bottle_name]['formula'] if bottle_name else None

        for p in _iter_platforms(descr, toolchain_name):
            pp = descr.get('params', {})

            res: ToolInfo = {
                'platform': p,
                'env': descr.get('env', {}),
                'params': pp,
                'formula': formula,
                'name': toolchain_key,
                'bottle_name': bottle_name,
                'executable_path': executable_path,
            }
            root = res.get('params', {}).get('match_root', None)

            if root:
                if formula and res.get('params', {}).get('use_bundle', False):
                    formula = yalibrary.fetcher.tool_chain_fetcher.get_formula_value(formula)
                    tool_var = platform_map.mapping_var_name_from_json(root, json.dumps(formula))
                else:
                    tool_var = pm.stringize_platform(p['target'], sep='_')

                res['tool_var'] = tool_var

                for key in ('env', 'params'):
                    res[key] = _subst(res[key], root, tool_var)

            yield res


def _platform_os_arch(plat: dict[str, typing.Any]) -> tuple[str | None, str | None]:
    os_ = plat.get('os')
    arch = plat.get('arch')
    return (
        os_.upper() if os_ else None,
        arch.upper() if arch else None,
    )


def resolve_tool(
    name: str | typing.Iterable[str], host: str, target: str, toolchain_key: str | None = None
) -> ToolInfo:
    name_parts = _split_tool_name(name) if isinstance(name, str) else tuple(name)
    tool_name = _join_tool_name(name_parts)
    match_os_arch_only = toolchain_key is not None
    parsed_host = _platform_os_arch(pm.parse_platform(host)) if match_os_arch_only else None
    parsed_target = _platform_os_arch(pm.parse_platform(target)) if match_os_arch_only else None

    def filter_host() -> typing.Iterator[ToolInfo]:
        avail = set()
        ok = False
        tn_filter = (lambda key, descr: key == toolchain_key) if toolchain_key else None

        for tool in iter_tools(name_parts, tn_filter):
            host_str = pm.stringize_platform(tool['platform']['host'])
            avail.add(host_str)
            if match_os_arch_only:
                matched = _platform_os_arch(tool['platform']['host']) == parsed_host
            else:
                matched = host_str == host
            if matched:
                ok = True
                yield tool

        if not ok:
            raise UnsupportedPlatform(
                'Unsupported host platforms %s for tool %s, use one of %s' % (host, tool_name, ', '.join(sorted(avail)))
            )

    target_match = (lambda plat: _platform_os_arch(plat) == parsed_target) if match_os_arch_only else None

    return _resolve_tool(tool_name, target, filter_host(), target_match=target_match)


def _resolve_tool_by_host_os(name: str, host_os: str, target: str) -> ToolInfo:
    def filter_host() -> typing.Iterator[ToolInfo]:
        avail = set()
        ok = False

        for tool in iter_tools(name):
            host_os_str = tool['platform']['host']['os']
            avail.add(host_os_str)
            if host_os_str == host_os:
                ok = True
                yield tool

        if not ok:
            raise UnsupportedPlatform(
                'Unsupported host os %s for tool %s, use one of %s' % (host_os, name, ', '.join(sorted(avail)))
            )

    return _resolve_tool(name, target, filter_host())


def _resolve_tool(
    name: str,
    target: str,
    tools: typing.Iterable[ToolInfo],
    target_match: typing.Callable[[dict[str, typing.Any]], bool] | None = None,
) -> ToolInfo:
    avail = set()
    for tool in tools:
        target_str = pm.stringize_platform(tool['platform']['target'])
        avail.add(target_str)

        if target_match is not None:
            matched = target_match(tool['platform']['target'])
        else:
            matched = target_str == target

        if matched:
            return tool

    raise UnsupportedPlatform(
        'Unsupported target platform %s for tool %s, use one of %s' % (target, name, ', '.join(sorted(avail)))
    )


def _split_tool_name(name: str) -> tuple[str, ...]:
    return tuple(name.split(_TOOL_NAME_SEPARATOR))


def _join_tool_name(parts: typing.Iterable[str]) -> str:
    return _TOOL_NAME_SEPARATOR.join(parts)


class TierInfo:
    def __init__(
        self, tier: str | None = None, deprecation_cause: str | None = None, revised: str | None = None
    ) -> None:
        self.tier = tier or ToolTier.UNSUPPORTED
        self.deprecation_cause = deprecation_cause
        self.revised = revised


class ToolSupport:
    def __init__(
        self,
        telegram: str | None = None,
        messenger: str | None = None,
        tracker: str | None = None,
        **kwargs: typing.Any,
    ) -> None:
        self.telegram = telegram
        self.messenger = messenger
        self.tracker = tracker


class _ToolConfig:
    def __init__(
        self,
        name_parts: tuple[str, ...],
        type: str,
        description: str,
        availability: str = ToolAvailability.FULL,
        tier: TierInfo | None = None,
        skill: str | None = None,
        owners: list[str] | None = None,
        support: dict[str, typing.Any] | None = None,
        docs: str | None = None,
        source: str | None = None,
        examples: list[str] | None = None,
        releases: str | None = None,
        tags: list[str] | None = None,
        host_platforms: list[str] | None = None,
        toolchains: dict[str, typing.Any] | None = None,
        bottles: dict[str, typing.Any] | None = None,
        **kwargs: typing.Any,  # swallow unknown config attributes
    ) -> None:
        self.name_parts = name_parts
        self.type = type
        self.description = description
        self.availability = availability
        self.tier = tier
        self.skill = skill
        self.owners = owners or []
        self.support = ToolSupport(**support) if support else None
        self.docs = docs
        self.source = source
        self.examples = examples or []
        self.releases = releases
        self.host_platforms = host_platforms
        self.tags = tags
        self.toolchains = toolchains or {}
        self.bottles = bottles or {}

    @property
    def name(self) -> str:
        return _join_tool_name(self.name_parts)


class _ToolConfigReader:
    _CFG_PATH_SEP = "/"
    _TOOLS_CFG_DIR = "tools/tools"
    _TOOLCHAIN_CFG_DIR = "tools/toolchains"
    _INTERNAL_CFG_DIR = "tools/internal"
    _TIERS_CFG = _CFG_PATH_SEP.join([_INTERNAL_CFG_DIR, "tiers.json"])
    _TOOL_SFX = ".tool.json"
    _TOOLCHAIN_SFX = ".toolchain.json"
    _LEGACY_TOOLS_KEY = "tools"
    _LEGACY_TOOLCHAINS_KEY = "toolchain"
    _TOOLCHAINS_KEY = "toolchains"
    _TOOLCHAIN_ALIASES_KEY = "toolchain_aliases"
    _BOTTLES_KEY = "bottles"
    _DEFAULT_FORMULA_PATH = "build/external_resources/{}/resources.json"

    def __init__(self) -> None:
        self._tool_cache = {}
        self._toolchain_aliases = {}
        self._tool_toolchains = defaultdict(dict)
        self._tiers = None

    def tool(self, name_parts: tuple[str, ...]) -> _ToolConfig:
        if name_parts in self._tool_cache:
            return self._tool_cache[name_parts]

        name = _join_tool_name(name_parts)

        if len(name_parts) > 1:
            parent_tool = self.tool(name_parts[:-1])
            if parent_tool.type != ToolType.PARENT:
                raise ToolNotFoundException("Tool '{}' doesn't exists".format(name))

        tool_cfg_file = self._make_cfg_path(self._TOOLS_CFG_DIR, name_parts, ext=self._TOOL_SFX)
        try:
            raw_tool_cfg = devtools.ya.core.config.get_tool_config(tool_cfg_file)["tool"]
            logger.debug("Find config for tool '{}' in {}".format(name, tool_cfg_file))
        except devtools.ya.core.config.MissingConfigError:
            logger.debug("Config {} for tool '{}' doesn't exist".format(tool_cfg_file, name))
            raw_tool_cfg = None

        tool_cfg = None
        if raw_tool_cfg:
            tier = self._get_tool_tier(name_parts, raw_tool_cfg=raw_tool_cfg)
            tool_type = raw_tool_cfg.get("type")
            if tool_type == ToolType.SIMPLE:
                tool_cfg = self._build_simple_tool(raw_tool_cfg, name_parts, with_toolchains=True)
            elif tool_type == ToolType.PARENT:
                tool_cfg = _ToolConfig(name_parts=name_parts, tier=tier, **raw_tool_cfg)
            elif tool_type == ToolType.TOOLCHAIN:
                tool_cfg = _ToolConfig(name_parts=name_parts, tier=tier, **raw_tool_cfg)
            else:
                raise ToolResolveException("Unknown tool type {} for tool '{}'".format(tool_type, name))

        if not tool_cfg or tool_cfg.type == ToolType.TOOLCHAIN:
            self._load_toolchains()
            if not tool_cfg:
                tool_cfg = self._build_legacy_tool(name_parts)
            tool_cfg.toolchains = self._tool_toolchains[name_parts].get("toolchains", {})
            tool_cfg.bottles = self._tool_toolchains[name_parts].get("bottles", {})
            host_platforms = []
            for toolchain in tool_cfg.toolchains.values():
                for platform in toolchain["platforms"]:
                    if platform.get("default", False):
                        host_platform = platform["host"]
                        host_platforms.append(
                            "-".join([host_platform["os"].lower(), host_platform.get("arch", _DEFAULT_ARCH).lower()])
                        )
            tool_cfg.host_platforms = sorted(host_platforms)

        self._tool_cache[name_parts] = tool_cfg
        return tool_cfg

    def toolchain_aliases(self) -> dict[str, str]:
        self._load_toolchains()
        return self._toolchain_aliases

    def list_tools(self, parent_parts: tuple[str, ...] | None = None) -> list[_ToolConfig]:

        parent_parts = parent_parts or ()
        tool_cfgs = {}
        tool_files = devtools.ya.core.config.list_tool_configs(self._make_cfg_path(self._TOOLS_CFG_DIR, parent_parts))
        for file in tool_files:
            if not file.endswith(self._TOOL_SFX):
                continue
            name_parts = parent_parts + (file[: -len(self._TOOL_SFX)],)
            path = self._make_cfg_path(self._TOOLS_CFG_DIR, parent_parts, file)
            logger.debug("Load tool from {}".format(path))
            raw_tool_cfg = devtools.ya.core.config.get_tool_config(path)["tool"]
            tool_cfg = self._build_simple_tool(raw_tool_cfg, name_parts)
            tool_cfgs[name_parts] = tool_cfg

        if not parent_parts:
            self._add_legacy_tools(tool_cfgs)

        return list(tool_cfgs.values())

    def _make_cfg_path(self, *parts: str | typing.Iterable[str], ext: str = "") -> str:
        final_parts: list[str] = []
        for part in parts:
            final_parts.extend([part] if isinstance(part, str) else part)
        return self._CFG_PATH_SEP.join(final_parts) + ext

    def _update_tool_toolchains(self, config: dict[str, typing.Any], toolchains_key: str) -> None:
        for tc_name, tc_def in config[toolchains_key].items():
            for tool_name, tool_def in tc_def["tools"].items():
                name_parts = _split_tool_name(tool_name)
                self._tool_toolchains[name_parts].setdefault("toolchains", {})
                self._tool_toolchains[name_parts].setdefault("bottles", {})
                self._tool_toolchains[name_parts]["toolchains"].setdefault(tc_name, tc_def)
                if "bottle" in tool_def:
                    bottle_name = tool_def["bottle"]
                    self._tool_toolchains[name_parts]["bottles"].setdefault(
                        bottle_name, config[self._BOTTLES_KEY][bottle_name]
                    )

    def _load_legacy_toolchains(self) -> None:
        legacy_config = devtools.ya.core.config.config()
        for name, alias in legacy_config.get(self._TOOLCHAIN_ALIASES_KEY, {}).items():
            self._toolchain_aliases.setdefault(name, alias)
        self._update_tool_toolchains(legacy_config, self._LEGACY_TOOLCHAINS_KEY)

    def _load_toolchains(self) -> None:
        if self._tool_toolchains:
            return
        toolchain_files = devtools.ya.core.config.list_tool_configs(self._TOOLCHAIN_CFG_DIR)
        for file in toolchain_files:
            if not file.endswith(self._TOOLCHAIN_SFX):
                continue
            path = self._make_cfg_path(self._TOOLCHAIN_CFG_DIR, file)
            logger.debug("Load toolchains from {}".format(path))
            cfg = devtools.ya.core.config.get_tool_config(path)
            self._update_tool_toolchains(cfg, self._TOOLCHAINS_KEY)
            self._toolchain_aliases.update(cfg.get(self._TOOLCHAIN_ALIASES_KEY, {}))

        self._load_legacy_toolchains()

    def _get_tool_tier(
        self, name_parts: tuple[str, ...], raw_tool_cfg: dict[str, typing.Any] | None = None
    ) -> TierInfo:
        if raw_tool_cfg and "deprecation_cause" in raw_tool_cfg:
            # Tool is deprecated by a tool author
            return TierInfo(ToolTier.DEPRECATED, deprecation_cause=raw_tool_cfg["deprecation_cause"])
        if not devtools.ya.core.config.supports_tool_tiers():
            return TierInfo(ToolTier.UNSPECIFIED)
        if self._tiers is None:
            try:
                self._tiers = devtools.ya.core.config.get_tool_config(self._TIERS_CFG).get("tiers", {})
            except devtools.ya.core.config.MissingConfigError as e:
                # XXX Raise exception when the tier config becomes mandatory
                logger.debug("Tiers config is not found: %s", e)
                self._tiers = {}
        # The tier of a child is equal to the tier of its root parent
        root_name = name_parts[0]
        tier_cfg = self._tiers.get(root_name)
        if tier_cfg:
            return TierInfo(**tier_cfg)
        if raw_tool_cfg and raw_tool_cfg.get("owners"):
            tier = ToolTier.COMMUNITY
        else:
            tier = ToolTier.UNSUPPORTED
        return TierInfo(tier=tier)

    def _build_toolchains_and_bottles(
        self, raw_tool_cfg: dict[str, typing.Any], name_parts: tuple[str, ...]
    ) -> tuple[dict[str, typing.Any], dict[str, typing.Any], list[str]]:
        name = _join_tool_name(name_parts)
        definition = raw_tool_cfg.get("definition", {})
        formula = definition.get("formula")
        env = definition.get("env")
        platforms = definition.get("platforms")
        executable = definition.get("executable")
        if formula is None:
            formula = self._DEFAULT_FORMULA_PATH.format("/".join(name_parts))
        if platforms is None:
            platforms = yalibrary.fetcher.tool_chain_fetcher.get_formula_platforms(formula)
        if not platforms:
            raise ToolResolveException("Allowed platforms are not specified or empty for tool '{}'".format(name))
        if executable is None:
            executable = [name_parts[-1]]
        elif isinstance(executable, str):
            executable = [executable]
        if env is None:
            env = {}

        tc_platforms = []
        for platform in platforms:
            tc_platform = {"default": True, "host": {}}
            p_parts = platform.split("-")
            tc_platform["host"]["os"] = p_parts[0].upper()
            if len(p_parts) > 1:
                tc_platform["host"]["arch"] = p_parts[1].lower()
            tc_platforms.append(tc_platform)
        # Auto-updated tools use bottle name as a part of the symlink name.
        # It's a bit more safe to use file names without spaces.
        bottle_name = ".".join(name_parts)

        toolchains = {
            name: {
                "tools": {
                    name: {
                        "bottle": bottle_name,
                        "executable": name,
                    },
                },
                "platforms": tc_platforms,
                "env": env,
            },
        }
        bottles = {
            bottle_name: {
                "formula": formula,
                "executable": {
                    name: executable,
                },
            },
        }
        return toolchains, bottles, platforms

    def _build_simple_tool(
        self, raw_tool_cfg: dict[str, typing.Any], name_parts: tuple[str, ...], with_toolchains: bool = False
    ) -> _ToolConfig:
        toolchains = None
        bottles = None
        platforms = raw_tool_cfg.get("definition", {}).get("platforms")
        if with_toolchains:
            toolchains, bottles, platforms = self._build_toolchains_and_bottles(raw_tool_cfg, name_parts)
        return _ToolConfig(
            name_parts=name_parts,
            tier=self._get_tool_tier(name_parts, raw_tool_cfg=raw_tool_cfg),
            toolchains=toolchains,
            bottles=bottles,
            host_platforms=sorted(platforms) if platforms else None,
            **raw_tool_cfg,
        )

    @property
    def _legacy_tools(self) -> dict[str, typing.Any]:
        return devtools.ya.core.config.config()[self._LEGACY_TOOLS_KEY]

    def _build_legacy_tool(self, name_parts: tuple[str, ...]) -> _ToolConfig:
        name = _join_tool_name(name_parts)
        if len(name_parts) != 1:
            raise ToolNotFoundException('Cannot find tool: ' + name)
        description = None
        availability = None
        if legacy_tool := self._legacy_tools.get(name):
            description = legacy_tool["description"]
            availability = ToolAvailability.FULL if legacy_tool.get("visible", True) else ToolAvailability.HIDDEN
        elif self._tool_toolchains.get(name_parts):
            # Not all tools are presented in tools section
            description = name
            availability = ToolAvailability.HIDDEN
        else:
            raise ToolNotFoundException('Cannot find tool: ' + name)
        return _ToolConfig(
            name_parts=name_parts,
            type="toolchain",
            description=description,
            availability=availability,
            tier=self._get_tool_tier(name_parts),
        )

    def _add_legacy_tools(self, tool_cfgs: dict[tuple[str, ...], _ToolConfig]) -> None:
        for name in self._legacy_tools:
            name_parts = _split_tool_name(name)
            if name_parts not in tool_cfgs:
                tool_cfg = self._build_legacy_tool(name_parts)
                tool_cfgs[name_parts] = tool_cfg


# For test purpose
def reset_cache() -> None:
    global _tool_config_reader
    _tool_config_reader = _ToolConfigReader()


_tool_config_reader = _ToolConfigReader()
