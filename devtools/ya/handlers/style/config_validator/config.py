import functools
import tomllib
import typing as tp
from collections.abc import Mapping, Sequence
from pathlib import PurePath

import yaml

from . import rules


class RawConfig(tp.NamedTuple):
    content: str
    name: str


def parse_toml(config: RawConfig) -> dict:
    return tomllib.loads(config.content)


def parse_yaml(config: RawConfig) -> dict:
    config_ = yaml.safe_load(config.content) or {}
    assert isinstance(config_, dict)
    return config_


@functools.cache
def parse_toml_cached(config: RawConfig) -> dict:
    return parse_toml(config)


@functools.cache
def parse_yaml_cached(config: RawConfig) -> dict:
    return parse_yaml(config)


class SupportsLookup(tp.Protocol):
    TYPE: tp.ClassVar[tp.LiteralString]

    def lookup(self, path: str) -> rules.MaybeValue: ...

    @property
    def requires_validation(self) -> bool:
        """Whether config is eligible for checking against the set of rules."""
        ...


class _NullConfig:
    TYPE: tp.ClassVar[tp.LiteralString] = 'null'

    def __init__(self, *args, **kwargs) -> None:
        pass

    def lookup(self, path) -> rules.MaybeValue:
        return rules.NO_VALUE

    @property
    def requires_validation(self) -> bool:
        return False


class _DictConfig:
    TYPE: tp.ClassVar[tp.LiteralString] = 'dict'

    def __init__(self, raw_config: RawConfig, params: Mapping, *, cache: bool = False) -> None:
        """
        params: `config_settings.parameters` section from rules config
        """
        self._params = params
        self._config = self._load(raw_config, cache)

    def _load(self, raw_config: RawConfig, cache: bool) -> dict:
        ext = PurePath(raw_config.name).suffix
        if ext == '.yaml':
            config = parse_yaml_cached(raw_config) if cache else parse_yaml(raw_config)
        elif ext == '.toml':
            config = parse_toml_cached(raw_config) if cache else parse_toml(raw_config)
        else:
            raise ValueError(f'Unknown config extension: {ext}')

        if prefix := self._params.get('common_prefix', {}).get(raw_config.name):
            config = self.deepget(config, prefix)

        if not config:
            return {}

        assert isinstance(config, dict)
        return config

    @property
    def requires_validation(self) -> bool:
        return bool(self._config)

    @staticmethod
    def deepget(obj: Mapping, keys: Sequence[str], default: object = None) -> object:
        sentinel = object()
        for key in keys:
            try:
                obj = obj.get(key, sentinel)
            except AttributeError:
                return default
        return obj if obj is not sentinel else default

    def lookup(self, path) -> rules.MaybeValue:
        return self.deepget(self._config, path, default=rules.NO_VALUE)


def make(config: RawConfig | None, settings: Mapping, *, cache: bool = False) -> SupportsLookup:
    if config is None:
        return _NullConfig()

    type = settings['type']
    params = settings['parameters']

    match type:
        case _DictConfig.TYPE:
            return _DictConfig(config, params, cache=cache)
        case _:
            raise ValueError(f'Unknown config type: {type}')
