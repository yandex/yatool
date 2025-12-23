import functools
import json
from pathlib import Path

import devtools.ya.handlers.style.config as cfg
import devtools.ya.handlers.style.config_validator as cfgval
import devtools.ya.handlers.style.styler as stlr
import devtools.ya.handlers.style.target as trgt
import devtools.ya.test.const as const


@functools.cache
def _read_validation_configs(
    configs: tuple[str, ...] = tuple(const.LinterConfigsValidationRules.enumerate()),
) -> dict[str, str]:
    joined: dict[str, str] = {}
    root = Path(cfg.find_root())
    for config in configs:
        with (root / config).open() as afile:
            joined.update(json.load(afile))
    return joined


def get_validation_configs(
    styler: stlr.ConfigurableStyler, target: trgt.Target
) -> tuple[cfg.MaybeConfig, cfg.Config, Path] | None:
    if not stlr.is_configurable(styler):
        raise AssertionError('get_configs_to_validate must not be called on a non-configurable styler')
    rules_config_map = _read_validation_configs()
    if styler.name not in rules_config_map:
        return
    rules_config = Path(cfg.find_root()) / rules_config_map[styler.name]
    # XXX: There is an assumption that lookup is not terribly slow
    base_config = styler.config_finder.lookup_default_config()
    user_config = styler.config_finder.lookup_config(target.path)
    return base_config, user_config, rules_config


def validate(
    base_config: cfg.MaybeConfig,
    user_config: cfg.Config,
    rules_config: Path,
) -> str:
    if base_config and base_config.path == user_config.path:
        # Base config always satisfies the validation rules, no need to check it
        return ''

    raw_base = cfgval.RawConfig(base_config.path.read_text(), base_config.path.name) if base_config else None
    raw_user = cfgval.RawConfig(user_config.path.read_text(), user_config.path.name)
    raw_rules = cfgval.RawConfig(rules_config.read_text(), rules_config.name)

    if errors := cfgval.validate(raw_rules, raw_user, raw_base):
        return f'''\
Config validation failed for user config {user_config.path}; rules config: {rules_config}; base_config: {base_config}. Errors:
{'\n'.join(errors)}
'''
    return ''
