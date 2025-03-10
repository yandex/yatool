import typing as tp  # noqa: F401

import six

import logging
from devtools.ya.core.yarg.excs import ArgsBindingException
from devtools.ya.core.yarg.help_level import HelpLevel
from devtools.ya.core.yarg import hooks
from devtools.ya.core.yarg.hooks import BaseHook  # noqa: F401
from devtools.ya.core.yarg.hooks import SetValueHook
from exts.strtobool import strtobool


def return_true_if_enabled(x):
    try:
        return bool(strtobool(x))
    except ValueError:
        return False


class Consumer(object):
    free = False
    group = None
    subgroup = None
    visible = HelpLevel.BASIC

    def __init__(self):
        self.parts = [self]  # type: list[Consumer]

    def __add__(self, other):
        return Compound(self, other)

    def formatted(self, opt):
        # type: ("Options") -> tp.Optional[str]
        """Returns formatted info in help about consumer"""
        return None


class Compound(Consumer):
    def __init__(self, *items):
        self.parts = []
        for item in items:
            self.parts += getattr(item, 'parts', [item])

    def __repr__(self):
        return "<Compound consumer from {} consumers>".format(len(self.parts))


class BaseArgConsumer(Consumer):
    def consume_args(self, opt, args):
        # type: ("Options", tp.Sequence[str]) -> list[str]
        """Used to process args one-by-one, returns remaining argument or raise exception or"""
        raise NotImplementedError()


class FreeArgConsumer(BaseArgConsumer):
    free = True
    greedy = True
    group = None
    visible = HelpLevel.NONE

    def __init__(self, help=None, hook=None):
        Consumer.__init__(self)
        self.hook = hook
        self.help = help

    def consume_args(self, opt, args):
        if len(args) >= 1:
            self.hook(opt, args)
        return []


class SingleFreeArgConsumer(BaseArgConsumer):
    free = True
    greedy = False
    group = None
    visible = HelpLevel.NONE

    def __init__(self, help=None, hook=None, required=True):
        Consumer.__init__(self)
        self.hook = hook
        self.help = help
        self.required = required

    def consume_args(self, opt, args):
        if len(args) >= 1:
            self.hook(opt, args[0])
            return args[1:]
        elif self.required:
            raise ArgsBindingException(
                'Expected free argument{arg}, got nothing'.format(arg=' ' + self.help if self.help else '')
            )
        return []


class ArgConsumer(BaseArgConsumer):
    def __init__(self, names, help=None, hook=None, group=None, visible=None, subgroup=None, deprecated=False):
        Consumer.__init__(self)
        if isinstance(names, six.string_types):
            names = [names]
        self.short_name = self._extract_short_name(names)
        self.long_name = self._extract_long_name(names)
        self.hook = hook  # type: BaseHook
        self.help = help  # type: tp.Optional[str]
        self.group = group
        self.subgroup = subgroup
        self.deprecated = deprecated  # type: bool

        if visible is None:
            visible = HelpLevel.BASIC

        if visible is True:
            self.visible = HelpLevel.BASIC
        elif visible is False:
            self.visible = HelpLevel.NONE
        elif visible == 0:
            raise ValueError(
                "For `visibility` use True, False, level number (not 0), level name or `core.yarg.help_level.HelpLevel` instance"
            )
        elif isinstance(visible, int):
            self.visible = HelpLevel(visible)
        elif isinstance(visible, six.string_types):
            self.visible = HelpLevel.__members__[visible]
        elif isinstance(visible, HelpLevel):
            self.visible = visible
        else:
            logging.warning("Unknown visibility level %s for %s; will be hidden", visible, names)
            self.visible = HelpLevel.BASIC

    @staticmethod
    def _extract_short_name(names):
        res = [x for x in names if x.startswith('-') and not x.startswith('--')]
        if len(res) > 1:
            raise Exception('Too many short names', res)
        elif len(res) == 0:
            return None
        else:
            return res[0]

    @staticmethod
    def _extract_long_name(names):
        res = [x for x in names if x.startswith('--')]
        if len(res) > 1:
            raise Exception('Too many long names', res)
        elif len(res) == 0:
            return None
        else:
            return res[0]

    def consume_args(self, opt, args):
        cur_arg = args[0] if len(args) > 0 else None
        next_arg = args[1] if len(args) > 1 else None
        if cur_arg is not None:
            assert self.hook is not None, self
            if self.hook.need_value():
                if cur_arg == self.long_name:
                    if next_arg is None:
                        raise ArgsBindingException('Expected a value after {0}'.format(cur_arg))
                    self.hook(opt, next_arg)
                    return args[2:]
                elif self.long_name is not None and cur_arg.startswith(self.long_name + '='):
                    self.hook(opt, cur_arg[len(self.long_name) + 1 :])
                    return args[1:]
                elif cur_arg == self.short_name:
                    if next_arg is None:
                        raise ArgsBindingException('Expected a value after {0}'.format(cur_arg))
                    self.hook(opt, next_arg)
                    return args[2:]
                elif self.short_name is not None and cur_arg.startswith(self.short_name + '='):
                    self.hook(opt, cur_arg[len(self.short_name) + 1 :])
                    return args[1:]
                elif self.short_name is not None and cur_arg.startswith(self.short_name):
                    self.hook(opt, cur_arg[len(self.short_name) :])
                    return args[1:]
            else:
                if cur_arg == self.long_name:
                    self.hook(opt)
                    return args[1:]
                elif cur_arg == self.short_name:
                    self.hook(opt)
                    return args[1:]
                elif self.short_name is not None and cur_arg.startswith(self.short_name):
                    self.hook(opt)
                    return ['-' + args[0][len(self.short_name) :]] + args[1:]
        return None

    def formatted(self, opt):
        def transform_name(name):
            if self.deprecated:
                res = '[[unimp]]' + name
            else:
                res = '[[good]]{}[[rst]]'.format(name)

            if self.hook.need_value():
                res += '=' + self.hook.name.upper()
            if self.deprecated:
                res += '[[unimp]] (deprecated)[[rst]]'
            return res

        names = [x for x in [self.short_name, self.long_name] if x is not None]

        import yalibrary.display

        res = '    '
        keys = ', '.join(transform_name(x) for x in names)
        keys_len = len(', '.join(yalibrary.display.strip_markup(transform_name(x)) for x in names))
        res += keys
        if self.help:
            if keys_len < 20:
                res += ' ' * (20 - keys_len)
                res += self.help
            else:
                res += '\n' + (' ' * 24) + self.help

        default_ = self.hook.default(opt)

        if default_ is not None:
            res += ' ' + default_

        available_options = self.hook.available_options(opt)

        if available_options is not None:
            res += ' ' + available_options

        return res


class NullArgConsumer(ArgConsumer):
    def __init__(self, name, *, need_value):
        if need_value:
            hook = hooks.SwallowValueDummyHook()
        else:
            hook = hooks.NoValueDummyHook()
        super().__init__(
            name,
            group=None,
            help="Does nothing. DO NOT USE",
            visible=False,
            hook=hook,
        )


class EnvConsumer(Consumer):
    def __init__(self, name, help=None, hook=None):
        Consumer.__init__(self)
        self.name = name
        self.help = help
        self.hook = hook

    def consume_env_var(self, opt, env):
        if self.hook.need_value():
            self.hook(opt, env)
        else:
            self.hook(opt)
        return []


class ConfigConsumer(Consumer):
    def __init__(self, name, hook=None, help=None, group=None):
        super(ConfigConsumer, self).__init__()
        self.name = name
        self.hook = hook or SetValueHook(self.name)
        self.help = help
        self.group = group

    def consume_config_value(self, opt, value):
        if self.hook.need_value():
            self.hook(opt, value)
        else:
            self.hook(opt)
        return []


def get_consumer(opt):
    # type: ("Options") -> Consumer
    consumer = opt.consumer()

    if isinstance(consumer, (list, tuple)):
        if opt.visible:
            for c in consumer:
                if isinstance(c, ArgConsumer):
                    c.visible = opt.visible

        consumer = sum(consumer, Consumer())
    else:
        if opt.visible:
            if isinstance(consumer, ArgConsumer):
                consumer.visible = opt.visible

    return consumer
