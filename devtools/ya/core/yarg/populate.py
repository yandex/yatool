import typing as tp  # noqa: F401
import six

import logging
import os
import re

from devtools.ya.core.config import find_root
from core.yarg.aliases import Alias
from core.yarg.config_files import apply_config, load_config_by_file
from core.yarg.consumers import Consumer  # noqa: F401
from core.yarg.consumers import EnvConsumer, BaseArgConsumer, get_consumer
from core.yarg.excs import ArgsBindingException
from core.yarg.options import Options  # noqa: F401
from core.yarg.options import _MergedOptions


logger = logging.getLogger(__name__)


class _Populate(object):
    def __init__(self, opt, args, env=None, unknown_args_as_free=False, config_files=None, prefix=None):
        # type: (Options, list[six.string_types], tp.Optional[dict], bool, list[str], list[str]) -> None
        self.opt = opt
        self.args = args
        self.env = env
        self.unknown_args_as_free = unknown_args_as_free
        self.config_files = config_files
        self.prefix = (prefix or [])[1:]  # skip ya

        self._aliases = []

        self.consumer = get_consumer(self.opt)  # type: Consumer

    def populate(self):
        if self.config_files:
            self._populate_config()

        if isinstance(self.opt, _MergedOptions):
            # Reload consumers for apply_config
            self.consumer = get_consumer(self.opt)

        self._populate_env()

        self._populate_args()

        self.opt.postprocess()
        self.opt.postprocess2(self.opt)

    def _populate_config(self):
        # TODO: Do not rewrite env, it's hard to test env aliases
        self.env = dict(os.environ)
        self.env['YA_ARCADIA_ROOT'] = find_root(fail_on_error=False)

        config_files = list(self.config_files)
        loaded_files = set()

        while len(config_files) > 0:
            config_file = config_files.pop(0)
            config = load_config_by_file(config_file)
            loaded_files.add(config_file)

            if config is None:
                continue

            # _original_config = config.copy()
            resolve_env_vars(config, self.env)
            logger.debug("Use config file `%s` with `%d` keys", config_file, len(config))

            self._inject_aliases(tuple(self._extract_aliases(config_file, config)))

            for include_path in self._extract_includes(config):
                if config_file not in self.config_files:
                    logger.warning("Skipping nested include `%s` from `%s`", include_path, config_file)
                    continue

                if (include_path in config_files) or (include_path in loaded_files):
                    logger.warning("Multiple inclusions of `%s`", include_path)
                    continue

                logger.debug("Including config file `%s` from `%s`", include_path, config_file)

                config_files.append(include_path)

            apply_config(self.opt, self.consumer, config, self._get_prefix_config(config))

    def _get_prefix_config(self, config):
        # type: (dict) -> dict
        sub_config = {}

        if self.prefix:
            sub_config = config
            for arg in self.prefix:
                sub_config = sub_config.get(arg, {})

        return sub_config

    def _extract_aliases(self, config_file, config):
        # type: (str, dict) -> tp.Iterable[Alias]
        aliases = config.get('alias', tuple())  # type: tp.Sequence[dict]
        for alias in aliases:
            alias_info = Alias(config_file, alias, self._get_prefix_config(alias))

            if not alias_info.check():
                continue

            yield alias_info

    def _inject_aliases(self, aliases):
        # type: (tp.Iterable[Alias]) -> None

        if self.opt._param_as_args:
            logger.debug("Aliases not injected, because ParamsAsArgs used")
            return

        if not aliases:
            logger.debug("No aliases to inject")
            return

        logger.debug("Inject aliases")
        consumers_count = 0

        for alias_info in aliases:  # type: Alias
            # Create Options
            if isinstance(self.opt, _MergedOptions):
                opt = alias_info.generate_options()
                self.opt.append(opt)

            alias_consumers = alias_info.generate_consumers()
            for alias_consumer in alias_consumers:
                self.consumer += alias_consumer

            consumers_count += len(alias_consumers)

        if aliases and not isinstance(self.opt, _MergedOptions):
            logger.warning(
                "Received opt is not extendable in this handler, so aliases "
                "can't be found in help. Please, contact with DEVTOOLSSUPPORT",
            )
            logger.debug("Expect `%s`, in fact `%s`", _MergedOptions.__name__, type(self.opt).__name__)
        logger.debug(
            "Aliases initialisation complete, found %d consumers from %d aliases", consumers_count, len(aliases)
        )

        self._aliases.extend(aliases)

    def _extract_includes(self, config):
        # type: (dict) -> tp.Iterable[str]
        includes = config.get("include", tuple())  # type: tp.Sequence[dict]

        for include in includes:
            resolve_env_vars(include, self.env)

            path = include.get("path")

            if not path:
                continue

            yield path

    def _populate_env(self):
        if self.env is not None:
            env_consumer = [x for x in self.consumer.parts if isinstance(x, EnvConsumer)]
            for cons in env_consumer:
                if cons.name in self.env:
                    cons.consume_env_var(self.opt, self.env.get(cons.name))

    def _populate_args(self):
        args_consumer = [x for x in self.consumer.parts if isinstance(x, BaseArgConsumer)]
        not_free_args_consumer = [x for x in args_consumer if not x.free]  # TODO: Use isinstance
        free_args_consumer = [x for x in args_consumer if x.free]
        if any(x.greedy for x in free_args_consumer) and any(x.greedy for x in free_args_consumer[:-1]):
            raise Exception('Cannot define free consumers after a greedy free consumer: {0}'.format(free_args_consumer))
        extra_args = self._split_args(self.args, not_free_args_consumer)
        for current_free_consumer in free_args_consumer:
            extra_args = current_free_consumer.consume_args(self.opt, extra_args)
            if extra_args is None:
                raise ArgsBindingException('Unmatched free args {0}'.format(extra_args))
        if extra_args:
            raise ArgsBindingException('Do not know what to do with free args {0}'.format(extra_args))

    def _split_args(self, args, consumers):
        """Args processing"""
        result = []
        while len(args) > 0:
            go = [consumer.consume_args(self.opt, args) for consumer in consumers]
            go = [x for x in go if x is not None]
            if len(go) == 0:
                if args[0] == '--':
                    return result + args[1:]

                if args[0].startswith('-') and args[0] != '-' and not self.unknown_args_as_free:
                    error_msg = 'Do not know what to do with {0} argument'.format(args[0])

                    if args[0].startswith('--'):
                        import pylev

                        dist = {
                            c.long_name: pylev.damerau_levenshtein(args[0], c.long_name)
                            for c in consumers
                            if c.long_name and len(c.long_name) > 3
                        }
                        top = sorted(dist.items(), key=lambda x: x[1])[:3]
                        top = [x for x in top if x[1] < 3]
                        if top:
                            error_msg += ". Did you mean {}?".format(', '.join("'{}'".format(x[0]) for x in top))

                    raise ArgsBindingException(error_msg)

                result.append(args[0])
                args = args[1:]
            elif len(go) == 1:
                args = go[0]
            else:
                raise ArgsBindingException('Conflicting consumers for', args, consumers)

        return result


def populate(opt, args, env=None, unknown_args_as_free=False, config_files=None, prefix=None):
    # type: (Options, list[six.string_types], tp.Optional[dict], bool, list[str], list[str]) -> None
    populator = _Populate(opt, args, env, unknown_args_as_free, config_files, prefix)
    populator.populate()


def resolve_env_vars(config, env=None):
    # type: (dict, tp.Optional[dict]) -> dict
    # TODO: Move to populate
    env = env or dict(os.environ)
    env_macro = re.compile(r'\$\{(?P<id>\w+)\}')

    def normalize_config(data):
        subst_env = lambda v: (  # noqa: E731
            env_macro.sub(lambda m: env.get(m.group('id'), m.group(0)), v) if isinstance(v, (six.string_types)) else v
        )
        return dict((k, subst_env(v)) for k, v in six.iteritems(data))

    config.update(normalize_config(config))
    return config
