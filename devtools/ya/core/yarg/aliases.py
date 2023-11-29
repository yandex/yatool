import typing as tp  # noqa: F401

import six

import logging
from core.yarg.consumers import Consumer  # noqa: F401
from core.yarg.consumers import ArgConsumer, EnvConsumer, ConfigConsumer
from core.yarg.hooks import BaseHook
from core.yarg.config_files import apply_config
from core.yarg.options import Options
from core.yarg import Params  # noqa: F401

logger = logging.getLogger(__name__)


class Alias(object):
    DEFAULT_GROUP = None
    SETTINGS_KEY = '_settings'

    def __init__(self, source, *configs):
        # type: (str, dict) -> None
        self.configs = configs

        self._config = {}  # Do not use this directly, use self.configs
        self._settings = {}

        for config in self.configs:
            if self.SETTINGS_KEY not in config:
                continue
            self._settings.update(config.pop(self.SETTINGS_KEY))
            self._config.update(config)

        self._settings_found = bool(self._settings)  # We can found settings in prefixes
        self._config = dict(self._config)

        self.name = None
        self.identifiers = []
        self.source = source
        self.applying = False
        self.applied = False

        self._generated_consumers = None

    def _cleanup_prefix_settings(self, config):
        # type: (dict) -> tp.Iterable[tuple[str, tp.Any]]
        """If deep key has key `_settings` maybe it's prefix, we don't need apply it"""
        for key, values in six.iteritems(config):
            if '_settings' in values:
                self._settings_found = True
                continue
            yield key, values

    def check(self):
        if not self._settings_found:
            logger.warning(
                "You need to fill `%s` field for alias from `%s` with keys `%s` " "(check logs for more information)",
                self.SETTINGS_KEY,
                self.source,
                self._config.keys(),
            )
            logger.debug("Wrong alias from `%s`: `%s`", self.source, self._config)
        return bool(self._settings)

    def _generate_arg_consumer(self):
        arg_params = self._settings.pop('arg')  # type: dict

        from core.yarg.groups import Group

        arg_params['group'] = Group.search_in_registry(arg_params.get('group', None))

        consumer = ArgConsumer(
            hook=ApplyFromAliasHook(self, need_value=False),
            # Will be processed without new consumer
            **arg_params
        )

        self.identifiers.extend((consumer.short_name, consumer.long_name))

        return consumer

    def _generate_env_consumer(self):
        # TODO: Test
        env_params = self._settings.pop('env')

        consumer = EnvConsumer(hook=ApplyFromAliasHook(self), **env_params)

        self.identifiers.append(consumer.name)

        return consumer

    def _generate_conf_consumer(self):
        config_params = self._settings.pop('conf')
        # Can be used for composition
        consumer = ConfigConsumer(hook=ApplyFromAliasHook(self), **config_params)

        self.name = consumer.name

        self.identifiers.append(consumer.name)

        return consumer

    def generate_consumers(self):
        # type: () -> list[Consumer]
        if self._generated_consumers is None:
            consumers = []
            if 'arg' in self._settings:
                consumers.append(
                    self._generate_arg_consumer(),
                )

            if 'env' in self._settings:
                consumers.append(self._generate_env_consumer())

            if 'conf' in self._settings:
                consumers.append(self._generate_conf_consumer())

            if self._settings:
                for key in self._settings:
                    logger.warning("Settings key `%s` not supported in alias for `%s`", key, self.identifiers)

            self._generated_consumers = consumers

        return self._generated_consumers

    @property
    def _last_identifier(self):
        if self.name:
            return self.name

        if self.identifiers:
            return self.identifiers[-1].strip('-').replace('-', '_')

        return "?"

    def generate_options(self):
        # type: () -> AliasOptionsBase
        alias_consumers = self.generate_consumers()

        OptionsClassForAlias = type(
            'OptionsForAlias{}'.format(self._last_identifier.capitalize()), (AliasOptionsBase,), {}
        )

        return OptionsClassForAlias(self.name, alias_consumers, self)

    def __str__(self):
        from_ = "" if not self.source else " from {}".format(self.source)
        return "<{}: {}{}>".format(type(self).__name__, self.name or self._last_identifier, from_)


class ApplyFromAliasHook(BaseHook):
    def __init__(self, alias, need_value=True):
        # type: (Alias, bool) -> None
        self.alias = alias
        self.configs = self.alias.configs
        self._need_value = need_value

    def __call__(self, to, value=True):
        # type: (Options, bool) -> None
        if value is True:
            logger.debug("Apply config from alias: %s", self.alias)

            if self.alias.applying:
                # TODO: Add stack
                s = "Alias {} already applying".format(self.alias)
                if six.PY3:
                    raise RecursionError(s)  # noqa:F821
                else:
                    raise RuntimeError(s)

            if not self.alias.applied:
                self.alias.applying = True
                apply_config(to, None, *self.configs)
                self.alias.applying = False
                self.alias.applied = True

        elif value is False:
            logger.debug("Alias was implicit disable: %s", self.alias)
        else:
            raise NotImplementedError("Aliases with arguments ({}) not implemented yet (maybe never)".format(value))

    def need_value(self):
        return self._need_value


class AliasOptionsBase(Options):
    def __init__(self, option_name, consumers, alias):
        # type: (str, tp.Sequence[Consumer], Alias) -> None
        self._option_name = option_name

        if option_name:
            setattr(self, option_name, None)

        self._consumers = list(consumers)
        self._alias = alias

    def consumer(self):
        # type: () -> list
        return self._consumers

    def postprocess2(self, params):  # type: (Params) -> None
        # For raw_params
        # TODO: Check that alias not initialized yet
        # TODO: Initialise if getattr(option_name) is True
        # TODO: Maybe do not use ConfigConsumer for this, only apply in postprocess2
        pass
        # if self._option_name and getattr(self, self._option_name, False) is True:
        #     self._apply_config(self._alias)
