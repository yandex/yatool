from __future__ import annotations

import functools
import json
import logging
import os
import tempfile
import typing as tp
from collections.abc import Generator
from pathlib import PurePath, Path

import marisa_trie

import devtools.ya.core.config
import devtools.ya.core.resource
import devtools.ya.test.const as const
import devtools.ya.handlers.style.config_validator as cfgval

if tp.TYPE_CHECKING:
    from . import styler as stlr


logger = logging.getLogger(__name__)

type ConfigPath = Path
type MaybeConfig = Config | None


class Config(tp.NamedTuple):
    path: ConfigPath
    pretty: str
    user_defined: bool


@functools.cache
def _find_root() -> str:
    return devtools.ya.core.config.find_root(fail_on_error=False)


@functools.cache
def _read_file(path: Path) -> str:
    return path.read_text()


@functools.cache
def _read_validation_configs(paths: tuple[str, ...]) -> dict[str, str]:
    joined: dict[str, str] = {}
    root = Path(_find_root())
    for path in paths:
        with (root / path).open() as afile:
            joined.update(json.load(afile))
    return joined


def validate(
    user_config: ConfigPath,
    stylers: set[stlr.ConfigurableStyler],
    *,
    validation_configs: tuple[str, ...] = tuple(const.LinterConfigsValidationRules.enumerate()),
) -> Generator[tuple[stlr.ConfigurableStyler, Path, list[str]]]:
    raw_user = cfgval.RawConfig(user_config.read_text(), user_config.name)
    rules_config_map = _read_validation_configs(validation_configs)

    for styler in stylers:
        if styler.name not in rules_config_map:
            continue

        rules_config = Path(_find_root()) / rules_config_map[styler.name]
        raw_rules = cfgval.RawConfig(_read_file(rules_config), rules_config.name)

        base_config = styler.lookup_default_config()
        raw_base = cfgval.RawConfig(_read_file(base_config.path), base_config.path.name) if base_config else None

        errors = cfgval.validate(raw_rules, raw_user, raw_base)
        if errors:
            yield styler, rules_config, errors


@tp.runtime_checkable
class SupportsConfigLookup(tp.Protocol):
    def lookup_config(self, path: PurePath) -> Config: ...

    def lookup_default_config(self) -> MaybeConfig: ...


class ConfigMixin:
    def __init__(self, config_loaders: tuple[ConfigLoader, ...]):
        self._config_loaders = config_loaders

    def lookup_config(self, path: PurePath) -> Config:
        for loader in self._config_loaders:
            if config := loader.lookup(path):
                return config
        raise FileNotFoundError(f"Couldn't find config for target {path}")

    def lookup_default_config(self) -> MaybeConfig:
        for loader in self._config_loaders:
            if isinstance(loader, DefaultConfig) and (config := loader.lookup()):
                return config
        return


class ConfigLoader(tp.Protocol):
    def lookup(self, path: PurePath) -> MaybeConfig:
        """Given target path return config path if found"""
        ...


class DefaultConfig:
    def __init__(self, linter_name: str, *, defaults_file: str = "", resource_name: str = ""):
        assert defaults_file or resource_name, "At least one of 'defaults_file' or 'resource_name' must be provided"

        if config := self._from_file(linter_name, defaults_file):
            self._default_config = Config(config, str(config.relative_to(_find_root())), user_defined=False)
        elif config := self._from_resource(resource_name):
            self._default_config = Config(config, f'{resource_name} (from resource)', user_defined=False)
        else:
            self._default_config = None

    def _from_file(self, linter_name: str, defaults_file) -> Path | None:
        if defaults_file:
            try:
                config_map = devtools.ya.core.config.config_from_arc_rel_path(defaults_file)
            except Exception as e:
                logger.warning("Couldn't obtain config from fs, config file %s, error %s", defaults_file, repr(e))
            else:
                return Path(os.path.join(_find_root(), config_map[linter_name]))

    def _from_resource(self, resource_name: str) -> Path | None:
        if resource_name:
            try:
                content: bytes = devtools.ya.core.resource.try_get_resource(resource_name)  # type: ignore
            except Exception as e:
                logger.warning("Couldn't obtain config from memory, resource name %s, error %s", resource_name, repr(e))
            else:
                temp = tempfile.NamedTemporaryFile(delete=False)  # will be deleted by tmp_dir_interceptor
                temp.write(content)
                temp.flush()  # will be read from other subprocesses
                return Path(temp.name)

    def lookup(self, *args, **kwargs) -> MaybeConfig:
        return self._default_config


class AutoincludeConfig:
    def __init__(self, linter_name: str, *, autoinclude_files: tuple[str, ...] = const.AUTOINCLUDE_PATHS):
        self._linter_name = linter_name
        self._autoinclude_files = autoinclude_files

        self._root = _find_root()
        self._trie = self._load_trie()
        # for a given autoinclude path we can provide the same config for the same linter
        self._autoinc_to_conf = self._build_autoinc_to_conf()

    @classmethod
    @functools.cache
    def make(cls, linter_name: str, *, autoinclude_files: tuple[str, ...] = const.AUTOINCLUDE_PATHS) -> tp.Self:
        return cls(linter_name, autoinclude_files=autoinclude_files)

    def _load_trie(self) -> marisa_trie.Trie:
        paths = []
        if not self._root:
            logger.warning("Couldn't detect arcadia root. Autoincludes won't be used for configs lookup")
            return marisa_trie.Trie([])
        for afile in self._autoinclude_files:
            try:
                paths.extend(
                    os.path.join(self._root, path) for path in devtools.ya.core.config.config_from_arc_rel_path(afile)
                )
            except Exception as e:
                logger.warning(
                    "Couldn't load autoinclude paths due to error %s. Autoincludes won't be used for configs lookup",
                    repr(e),
                )
                paths = []
        return marisa_trie.Trie(paths)

    def _build_autoinc_to_conf(self) -> dict[str, Config]:
        # there may be a linter-specific logic to lookup the config
        # for now stick to sequential search and the config file existence
        map_ = {}
        for path in self._trie.keys():
            for config_name in const.LINTER_CONFIG_TYPES[self._linter_name]:
                config: Path = Path(path) / config_name

                if config.exists():
                    map_[path] = Config(config, str(config.relative_to(self._root)), user_defined=True)
        return map_

    def lookup(self, path: PurePath) -> MaybeConfig:
        keys: list[str] = self._trie.prefixes(str(path))
        if keys:
            autoinc_path = sorted(keys, key=len)[-1]
            return self._autoinc_to_conf.get(autoinc_path)


# TODO: delete after migration to autoincludes
class RuffConfig:
    _RUFF_CONFIG_PATHS_FILE = "build/config/tests/ruff/ruff_config_paths.json"

    def __init__(self, config_path_trie: str = _RUFF_CONFIG_PATHS_FILE):
        self._config_path_trie = config_path_trie
        self._ruff_trie: marisa_trie.Trie | None = None
        self._config_paths: list[str] = []
        self._root = _find_root()
        self._load_ruff_trie()

    def _load_ruff_trie(self) -> None:
        if not self._root:
            logger.warning("Couldn't detect arcadia root. Ruff config mapping won't be used for configs lookup")
            return
        try:
            config_map = {}
            for prefix, config_path in devtools.ya.core.config.config_from_arc_rel_path(self._config_path_trie).items():
                config_map[os.path.normpath(os.path.join(self._root, prefix))] = config_path
        except Exception as e:
            logger.warning(
                "Couldn't load ruff config mapping due to error %s. Ruff config mapping won't be used for configs lookup",
                repr(e),
            )
            return
        # Trie assigns indexes randomly, have to map back
        self._config_paths = [""] * len(config_map)
        self._ruff_trie = marisa_trie.Trie(config_map.keys())
        for prefix, idx in self._ruff_trie.items():  # type: ignore
            self._config_paths[idx] = os.path.join(self._root, config_map[prefix])

    def lookup(self, path: PurePath) -> MaybeConfig:
        if self._ruff_trie and (keys := self._ruff_trie.prefixes(str(path))):
            # even though there is `'': <default config>` in ruff_config_paths.json
            # keys can be empty if stdin is used, thus checking above
            key = sorted(keys, key=len)[-1]
            if key != self._root:
                config = Path(self._config_paths[self._ruff_trie[key]])
                return Config(config, str(config.relative_to(self._root)), user_defined=True)
