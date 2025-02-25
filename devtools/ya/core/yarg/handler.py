from __future__ import print_function

import logging
import os
import sys
import traceback
import typing as tp  # noqa: F401
from collections import defaultdict

import devtools.ya.core.yarg
import devtools.ya.core.report

import exts.strings
import exts.yjson as json

from six import iteritems

from devtools.ya.core.yarg.help import format_help, format_usage, format_examples, ShowHelpException
from devtools.ya.core.yarg.options import merge_opts
from devtools.ya.core.yarg.excs import ArgsBindingException
from devtools.ya.core.yarg.help_level import HelpLevel

from yalibrary.display import build_term_display


# XXX hack to preserve "ya m" as "ya make" short form
EXACT_MATCH_HANDLERS = [
    'maven-import',
    'tool',
]
EMPTY_KEY = '__EMPTY__'

logger = logging.getLogger(__name__)


def print_formatted(msg, stream=None):
    if stream is None:
        stream = sys.stdout
    build_term_display(stream, stream.isatty()).emit_message(msg)


class SimpleHandler(object):
    def dump(self):
        return type(self).__name__

    def handle(self, root_handler, args, prefix):
        # type: (BaseHandler, list[str], list[str]) -> None
        pass

    @property
    def visible(self):
        # type: () -> bool
        return True

    @property
    def description(self):
        # type: () -> str
        raise NotImplementedError()

    @property
    def options(self):
        # type: () -> tp.Optional[Options]
        return None

    @property
    def sub_handlers(self):
        # type: () -> tp.Optional[dict[str, SimpleHandler]]
        return None


class DumpHandler(SimpleHandler):
    def handle(self, root_handler, args, prefix):
        res = root_handler.dump()
        print(json.dumps(res, indent=4, sort_keys=True, separators=(',', ': ')))

    def dump(self):
        return type(self).__name__

    @property
    def visible(self):
        return False

    @property
    def description(self):
        return 'Dump handlers tree'

    @property
    def options(self):
        return None

    @property
    def sub_handlers(self):
        return None


class HelpHandler(SimpleHandler):
    def handle(self, root_handler, args, prefix):
        print_formatted(root_handler.format_help(prefix[:-1], examples=False))

    def dump(self):
        return type(self).__name__

    @property
    def visible(self):
        return False

    @property
    def description(self):
        return 'Show help message'

    @property
    def options(self):
        return None

    @property
    def sub_handlers(self):
        return None


class BaseHandler(SimpleHandler):
    def act(self, **kwargs):
        # type: (tp.Any) -> tp.Any
        raise NotImplementedError()

    def format_usage(self, prefix):
        # type: (str) -> str
        raise NotImplementedError()

    # TODO: Split usages
    def format_help(self, prefix):
        # type: (str) -> str
        raise NotImplementedError()

    def option_recursive(self, prefix):
        # type: (str) -> dict
        pass


def find_handler(arg, handlers):
    # type: (str, dict[str, BaseHandler]) -> tp.Optional[tuple[str, SimpleHandler]]

    if arg in handlers:
        return arg, handlers[arg]

    import pylev as pl

    ret = None

    for k, h in handlers.items():
        if k in EXACT_MATCH_HANDLERS:
            continue

        new_ret = None

        if k.startswith(arg):
            new_ret = h
        elif len(k) > 2:
            l_dist = pl.damerau_levenshtein(arg, k)

            if l_dist < 2:
                new_ret = h

        if new_ret:
            # too many similar handlers
            if ret:
                return None

            ret = (k, new_ret)

    return ret


class CompositeHandler(BaseHandler):
    help_handler = HelpHandler()

    @property
    def visible(self):
        return self._visible

    @property
    def description(self):
        return self._description

    @property
    def options(self):
        return None

    @property
    def sub_handlers(self):
        return dict((name, handler) for (name, handler) in iteritems(self._handlers))

    def __init__(self, description, visible=True, examples=None, extra_help=''):
        self._handlers = {
            '__DUMP__': DumpHandler(),
            '--help': self.help_handler,
            '-h': self.help_handler,
        }
        self._description = description
        self._visible = visible
        self._examples = examples or []
        self._extra_help = extra_help

    def __setitem__(self, key, value):
        self._handlers[key] = value

    def __getitem__(self, item):
        return self._handlers[item]

    def act(self, **kwargs):
        return self._handlers[kwargs.pop('handler')].act(**kwargs.pop('args', {}))

    def handle(self, root_handler, args, prefix):
        handlers = self._handlers

        if len(args) > 0:
            target = find_handler(args[0], handlers)
        else:
            target = None

        if target:
            name, handler = target
            return handler.handle(self, args[1:], prefix + [name])
        else:
            handlers = self.handlers_recursive(only_visible=False)

            if len(args) > 0 and args[0] in handlers:
                return handlers[args[0]].handle(self, args[1:], prefix + [args[0]])
            elif EMPTY_KEY in handlers:
                return handlers[EMPTY_KEY].handle(self, args, prefix)
            elif len(args) == 0:
                print_formatted(self.format_help(prefix, examples=False))
            else:
                raise ArgsBindingException("Can't handle args: " + str(' '.join(args)))

    def dump(self):
        result = {}
        for k in sorted(self._handlers.keys()):
            result[k] = self._handlers[k].dump()
        return result

    def handlers_recursive(self, only_visible=True):
        res = {}
        for key, value in iteritems(self._handlers):
            try:
                if not getattr(value, 'visible', True) and only_visible:
                    continue
                if key == EMPTY_KEY:
                    if getattr(value, 'composite', False):
                        res.update(value.handlers_recursive())
                    else:
                        res[key] = value
                else:
                    res[key] = value
            except Exception:
                logger.debug(traceback.format_exc())
        return res

    def opts_recursive(self, prefix):
        res = defaultdict(list)
        if self._examples:
            res[prefix + ('<subcommand>',)] = self._examples
        only_visible = True
        for key, value in iteritems(self._handlers):
            try:
                if not getattr(value, 'visible', True) and only_visible:
                    continue
                new_prefix = prefix if key == EMPTY_KEY else prefix + (key,)
                try:
                    res.update(value.opts_recursive(new_prefix))
                except AttributeError:
                    pass  # for junk plugins

            except Exception:
                logger.debug(traceback.format_exc())

        return res

    def format_usage(self, prefix):
        handlers = self.handlers_recursive()

        has_sub_commands = len([x for x in handlers.keys() if x != EMPTY_KEY]) > 0
        has_opts = EMPTY_KEY in handlers

        result = self._description + '\n\n'

        if self._extra_help:
            result += self._extra_help
        else:
            res = []

            if has_sub_commands:
                if has_opts:
                    res.append('[<subcommand>]')
                else:
                    res.append('<subcommand>')

            if has_opts:
                res.append(handlers[EMPTY_KEY].format_usage())

            result += '[[imp]]Usage[[rst]]: ' + ' '.join(prefix) + ' ' + ' '.join(res)

        return result + '\n'

    def format_help(self, prefix, examples=True):
        handlers = self.handlers_recursive()
        has_sub_commands = len([x for x in handlers.keys() if x != EMPTY_KEY]) > 0

        result = [self.format_usage(prefix)]

        if has_sub_commands:
            result.append('[[imp]]Available subcommands:[[rst]]')
            max_name_len = max([len(x) for x in handlers.keys()])
            for k in sorted(handlers.keys()):
                if handlers[k].visible:
                    name = k if k != EMPTY_KEY else '<empty>'
                    sub_handler = handlers[k]
                    desc = []
                    for i, line in enumerate(sub_handler.description.split('\n')):
                        if i != 0:
                            line = ' ' * (max_name_len + 5 + 3) + line  # + 3 spaces

                        desc.append(line)

                    result.append(
                        '  {name} {desc}'.format(name=name.ljust(max_name_len + 5, ' '), desc='\n'.join(desc))
                    )

        if examples:
            result.append(format_examples(self.opts_recursive(tuple(prefix))))

        if EMPTY_KEY in handlers:
            result.append(handlers[EMPTY_KEY].format_help())

        return '\n'.join(result)


class OptsHandler(BaseHandler):
    _latest_handled_prefix = None

    def __init__(
        self,
        action,
        description=None,
        opts=tuple(),
        visible=True,
        examples=None,
        unknown_args_as_free=False,
        use_simple_args=False,
        extra_help=None,
        stderr_help=None,
    ):
        self._action = action
        self._opt = merge_opts(opts)  # type: Options
        self._description = description
        self._extra_help = extra_help
        self._stderr_help = stderr_help
        self._unknown_args_as_free = unknown_args_as_free
        self._examples = examples or []
        self._visible = visible
        self._use_simple_args = use_simple_args  # call action with kwargs. preferred approach

    def act(self, **user_args):
        args = self._opt.params().__dict__.copy()
        for k, v in iteritems(user_args):
            if k in args:
                logger.debug('Change arg %s value %s -> %s', k, args[k], v)
                args[k] = v
            else:
                logger.warning('Skip arg %s:%s', k, v)
        if self._use_simple_args:
            return self._action(**args)
        else:
            return self._action(devtools.ya.core.yarg.Params(**args))

    @property
    def visible(self):
        return self._visible

    @property
    def description(self):
        return self._description

    @property
    def options(self):
        return self._opt

    @property
    def sub_handlers(self):
        return None

    @staticmethod
    def latest_handled_prefix():
        return OptsHandler._latest_handled_prefix

    def handle(self, root_handler, args, prefix):
        try:
            OptsHandler._latest_handled_prefix = prefix
            handler = {
                'args': [exts.strings.to_unicode(arg, exts.strings.guess_default_encoding()) for arg in args],
                'prefix': prefix,
            }

            devtools.ya.core.report.telemetry.report(
                devtools.ya.core.report.ReportTypes.HANDLER,
                handler,
            )

            try:
                import app_ctx

                app_ctx.handler_info['handler'] = handler
            except Exception:
                logger.debug("While storing handler_info", exc_info=True)
                pass

            params = self._opt.initialize(
                args,
                prefix=prefix,
                unknown_args_as_free=self._unknown_args_as_free,
                user_config=bool(int(os.getenv("YA_LOAD_USER_CONF", "1"))),
            )
        except ShowHelpException as exc:
            # XXX: remove copy/paste
            usage = self._description + '\n\n'
            usage += 'Usage:\n' + '  ' + ' '.join(prefix) + ' ' + self.format_usage() + '\n\n'
            if self._extra_help:
                usage += self._extra_help + '\n\n'
            usage += format_examples(self.opts_recursive(tuple(prefix)))
            usage += '\n' + self.format_help(exc.help_level)
            if (
                self._stderr_help
                and exc.help_level is HelpLevel.BASIC
                and sys.stderr.isatty()
                and not sys.stdout.isatty()
            ):
                # helpful when grepping
                print_formatted(self._stderr_help + '\n\n', sys.stderr)
            print_formatted(usage)
            sys.exit(0)

        if self._use_simple_args:
            return self._action(**params.__dict__)
        else:
            return self._action(params)

    def opts_recursive(self, prefix):
        return {prefix: self._examples}

    def dump(self):
        return '{name}({args})'.format(name=self._action.__name__, args=self._opt)

    def format_help(self, help_level=HelpLevel.BASIC):
        return format_help(self._opt, help_level)

    def format_usage(self):
        return format_usage(self._opt)


class FeedbackHandler(OptsHandler):
    YA_STDIN_PARAM = 'YA_STDIN'

    def __init__(self, root_handler):
        self._root_handler = root_handler
        super(FeedbackHandler, self).__init__(
            action=self.go,
            visible=False,
        )

    def go(self, params):
        args = os.environ.get(self.YA_STDIN_PARAM)
        if not args:
            args = sys.stdin.read()
            # Save it for a possible respawn.
            os.environ[self.YA_STDIN_PARAM] = args
        else:
            # We assume that YA_STDIN is not used as a source for
            # the first launch of ya -
            logger.debug('Read YA_STDIN from env')
            logger.debug('Unset YA_STDIN')
            del os.environ[self.YA_STDIN_PARAM]
        args = json.loads(args)
        return self._root_handler.act(**args)
