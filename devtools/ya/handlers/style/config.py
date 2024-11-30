from __future__ import annotations

import logging
import os
import tempfile
import typing as tp
from pathlib import PurePath, Path

import marisa_trie

import core.config
import core.resource
import devtools.ya.test.const as const
import exts.func


logger = logging.getLogger(__name__)


type Config = Path
type MaybeConfig = Config | tp.Literal[""]


@exts.func.lazy
def _find_root() -> str:
    return core.config.find_root()


def configurable(obj: object) -> bool:
    return issubclass(type(obj), ConfigMixin)


class ConfigMixin:
    def __init__(self, config_loaders: tuple[ConfigLoader, ...]):
        self._config_loaders = config_loaders

    def lookup(self, path: PurePath) -> Config:
        for loader in self._config_loaders:
            if config := loader.lookup(path):
                return config
        raise FileNotFoundError(f"Couldn't find config for target {path}")


class ConfigLoader(tp.Protocol):
    def lookup(self, path: PurePath) -> MaybeConfig:
        """Given target path return config path"""


class DefaultConfig:
    def __init__(self, linter_name: str, defaults_file: str = "", resource_name: str = ""):
        assert defaults_file or resource_name, "At least one of 'defaults_file' or 'resource_name' must be provided"
        self._default_config = self._load_default(linter_name, defaults_file, resource_name)

    def _load_default(self, linter_name: str, defaults_file: str, resource_name: str) -> MaybeConfig:
        if defaults_file:
            try:
                config_map = core.config.config_from_arc_rel_path(defaults_file)
            except Exception as e:
                logger.warning("Couldn't obtain config from fs, config file %s, error %s", defaults_file, repr(e))
            else:
                return Path(os.path.join(_find_root(), config_map[linter_name]))
        if resource_name:
            try:
                content = core.resource.try_get_resource(resource_name)
            except Exception as e:
                logger.warning("Couldn't obtain config from memory, resource name %s, error %s", resource_name, repr(e))
            else:
                temp = tempfile.NamedTemporaryFile(delete=False)  # will be deleted by tmp_dir_interceptor
                temp.write(content)
                temp.flush()  # will be read from other subprocesses
                return Path(temp.name)
        return ""

    def lookup(self, path: PurePath) -> MaybeConfig:
        return self._default_config


class AutoincludeConfig:
    def __init__(self, linter_name: str, autoinclude_files: tuple[str, ...] = const.AUTOINCLUDE_PATHS):
        self._linter_name = linter_name
        self._autoinclude_files = autoinclude_files

        self._trie = self._load_trie()
        # for a given autoinclude path we can provide the same config for the same linter
        self._autoinc_to_conf = self._build_autoinc_to_conf()

    def _load_trie(self) -> marisa_trie.Trie:
        paths = []
        root = _find_root()
        for afile in self._autoinclude_files:
            try:
                paths.extend(os.path.join(root, path) for path in core.config.config_from_arc_rel_path(afile))
            except Exception as e:
                logger.warning(
                    "Couldn't load autoinclude paths due to error %s. Autoincludes won't be used for configs lookup",
                    repr(e),
                )
                paths = []
        return marisa_trie.Trie(paths)

    def _build_autoinc_to_conf(self) -> dict[str, Path]:
        # there may be a linter-specific logic to lookup the config
        # for now stick to sequential search and the config file existence
        map_ = {}
        for path in self._trie.keys():
            for config_name in const.LINTER_CONFIG_TYPES[self._linter_name]:
                config = os.path.join(path, config_name)

                if os.path.exists(config):
                    map_[path] = Path(config)
        return map_

    def lookup(self, path: PurePath) -> MaybeConfig:
        keys = self._trie.prefixes(str(path))
        if keys:
            path = sorted(keys, key=len)[-1]
            return self._autoinc_to_conf[path]
        return ""


# TODO: delete after migration to autoincludes
class RuffConfig:
    _RUFF_CONFIG_PATHS_FILE = "build/config/tests/ruff/ruff_config_paths.json"

    def __init__(self):
        self._ruff_trie: marisa_trie.Trie | None = None
        self._config_paths: list[str] = []
        self._load_ruff_trie()

    def _load_ruff_trie(self) -> None:
        arc_root = _find_root()
        try:
            config_map = {}
            for prefix, config_path in core.config.config_from_arc_rel_path(self._RUFF_CONFIG_PATHS_FILE).items():
                config_map[os.path.normpath(os.path.join(arc_root, prefix))] = config_path
        except Exception:
            return
        # Trie assigns indexes randomly, have to map back
        self._config_paths = [""] * len(config_map)
        self._ruff_trie = marisa_trie.Trie(config_map.keys())
        for prefix, idx in self._ruff_trie.items():  # type: ignore
            self._config_paths[idx] = os.path.join(arc_root, config_map[prefix])

    def lookup(self, path: PurePath) -> MaybeConfig:
        if self._ruff_trie:
            keys = self._ruff_trie.prefixes(str(path))
            # keys is never empty because there is `'': <default config>`` in ruff_config_paths.json
            key = sorted(keys, key=len)[-1]
            if key != _find_root():
                return Path(self._config_paths[self._ruff_trie[key]])
        return ""
