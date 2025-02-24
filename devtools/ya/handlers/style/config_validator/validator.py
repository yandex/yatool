from . import config
from . import rules


def _validate(rules_config: dict, user_config: config.SupportsLookup, base_config: config.SupportsLookup) -> list[str]:
    """
    Go over the list rule settings and call an appropriate rule function.
    Return a list of errors.
    """
    errors = []
    for rule_props in rules_config['rules']:

        bvalue = base_config.lookup(rule_props['path'])
        uvalue = user_config.lookup(rule_props['path'])
        rule = rules.select_rule(rule_props['type'])

        error = rule(rule_props['rule'], uvalue, bvalue)

        if error:
            errors.append(f"Field path {rule_props['path']}: '{error}'")

    return errors


def validate(
    raw_rules: config.RawConfig,
    raw_user: config.RawConfig,
    raw_base: config.RawConfig | None = None,
) -> list[str]:
    # Our use case is to load rules and base configs many times, user config only once.
    # For this reason we use caching where appropriate.
    rconfig = config.parse_yaml_cached(raw_rules)

    config_settings = rconfig['config_settings']

    uconfig = config.make(raw_user, config_settings)

    if not uconfig.requires_validation:
        return []

    bconfig = config.make(raw_base, config_settings, cache=True)

    return _validate(rconfig, uconfig, bconfig)
