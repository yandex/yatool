from __future__ import print_function

import os
import six
import sys
import time
import signal
import devtools.ya.core.config
import logging
import argparse

import app_config

import devtools.ya.core.error
import devtools.ya.core.respawn
import devtools.ya.core.sec as sec
import devtools.ya.core.sig_handler
import devtools.ya.core.stage_tracer as stage_tracer
import devtools.ya.core.yarg

from devtools.ya.core.yarg import LazyCommand, try_load_handler
from devtools.ya.core.logger import init_logger
from devtools.ya.core.plugin_loader import explore_plugins
from library.python import mlockall

import devtools.ya.app

stager = stage_tracer.get_tracer("overall-execution")


def _mlockall():
    e = mlockall.mlockall_current()
    devtools.ya.app.MLOCK_STATUS_MESSAGE = "mlockall return code: {!s}".format(e)


def do_main(args, extra_help):
    import app_config

    plugin_map = explore_plugins(
        loader_hook=try_load_handler,
        suffix='_handler',
    )

    handler = devtools.ya.core.yarg.CompositeHandler(description=app_config.description, extra_help=extra_help)
    for plugin_name in sorted(plugin_map.names()):
        handler[plugin_name] = LazyCommand(plugin_name, plugin_map.get(plugin_name))
    handler['-'] = devtools.ya.core.yarg.FeedbackHandler(handler)
    stager.start("handler-selection")
    res = handler.handle(handler, args, prefix=['ya'])
    if isinstance(res, six.integer_types):
        sys.exit(res)


def do_intercepted(func, interceptors):
    def wrap(f, wrapper):
        return lambda: wrapper(f)

    wrapped_func = func
    for interceptor in reversed(interceptors):
        wrapped_func = wrap(wrapped_func, interceptor)

    return wrapped_func()


class BadArg(Exception):
    pass


class ArgParse(argparse.ArgumentParser):
    def parse_max(self, args):
        cur = []
        gen = iter(args)
        res = self.parse_args([])

        for arg in gen:
            cur.append(arg)

            try:
                res, bad = self.parse_known_args(cur)

                if bad:
                    return res, [arg] + list(gen)
            except BadArg:
                pass

        return res, []

    def error(self, s):
        raise BadArg(s)


def setup_faulthandler():
    try:
        import faulthandler
    except ImportError:
        return

    # Dump python backtrace in case of any errors
    faulthandler.enable()
    if hasattr(signal, "SIGQUIT"):
        faulthandler.register(signal.SIGQUIT, chain=True)


def format_msg(args):
    ts = args['_timestamp']
    tn = args['_typename']

    if 'Message' in args:
        try:
            msg = '(' + str(args['PID']) + ') ' + args['Message']
        except KeyError:
            msg = args['Message']
    elif 'Started' in tn:
        msg = 'start ' + args['StageName']
    elif 'Finished' in tn:
        msg = 'end ' + args['StageName']
    else:
        msg = str(args)

    return ts / 1000000.0, msg


def main(args):
    exit_handler = devtools.ya.core.sig_handler.create_sigint_exit_handler()
    signal.signal(signal.SIGINT, exit_handler)

    _mlockall()

    setup_faulthandler()

    opensource = not app_config.in_house
    p = ArgParse(prog='ya', add_help=False)

    # Do not forget add arguments with value to `skippable_flags` into /ya script
    p.add_argument('--precise', action='store_const', const=True, default=False, help='show precise timings in log')
    p.add_argument(
        '--profile', action='store_const', const=True, default=False, help='run python profiler for ya binary'
    )
    p.add_argument('--error-file')
    p.add_argument('--keep-tmp', action='store_const', const=True, default=False)
    p.add_argument(
        '--no-logs', action='store_const', const=True, default=True if os.environ.get('YA_NO_LOGS') else False
    )
    p.add_argument(
        '--no-new-pgroup',
        action='store_const',
        const=True,
        default=os.environ.get('YA_NEW_PGROUP') in ("0", "no"),
        help='make ya inherit process group id',
    )
    # Disable reports for opensource by default
    p.add_argument(
        '--no-report',
        action='store_const',
        const=True,
        default=True if os.environ.get('YA_NO_REPORT') in ("1", "yes") else opensource is True,
    )
    # after all events disabled using --no-report we can still enable one or more events
    # (using ',' separator for few events)
    p.add_argument(
        '--report-events',
        action='append',
        default=None if opensource is True else os.environ.get('YA_REPORT_EVENTS'),
    )
    p.add_argument(
        '--no-tmp-dir',
        action='store_const',
        const=True,
        default=True if os.environ.get('YA_NO_TMP_DIR') in ("1", "yes") else False,
    )
    p.add_argument('--no-respawn', action='store_const', const=True, default=False, help=argparse.SUPPRESS)
    p.add_argument('--print-path', action='store_const', const=True, default=False)
    p.add_argument('--version', action='store_const', const=True, default=False)
    p.add_argument(
        '-v',
        '--verbose-level',
        action='store_const',
        const=logging.DEBUG,
        default=logging.DEBUG if os.environ.get('YA_VERBOSE') else logging.INFO,
    )
    if not opensource:
        p.add_argument('--diag', action='store_const', const=True, default=False)

    a, args = p.parse_max(args[1:])

    if a.version:
        from library.python import svn_version

        print("\n".join(svn_version.svn_version().split("\n")[:-2]))
        INDENT = "    "
        print(INDENT + "Origin: {}".format("Github" if opensource else "Arcadia"))
        print(INDENT + "Python version: {}".format(sys.version).replace("\n", ""))

        sys.exit(0)

    if a.print_path:
        print(sys.executable)
        sys.exit(0)

    if a.profile:
        a.precise = True

    init_logger(a.verbose_level)

    if a.precise:
        start = time.time()

        replacements = sec.mine_suppression_filter(argv=args)

        class Handler(logging.StreamHandler):
            def emit(self, record):
                ts, msg = 0, None

                # Special format for events
                if isinstance(record.args, dict) and '_timestamp' in record.args:
                    try:
                        ts, msg = format_msg(record.args)
                    except KeyError:
                        pass

                if msg is None:
                    ts, msg = time.time(), record.getMessage()

                msg = six.ensure_str(msg).strip()
                sys.stderr.write("{}: {}\n".format(str(ts - start)[:10], sec.cleanup(msg, replacements)))

        logging.root.addHandler(Handler())

    def format_help():
        s = p.format_help().replace('[--diag]', '[--diag] [--help] <SUBCOMMAND> [OPTION]...').strip()

        for line in s.split('\n'):
            if line:
                yield line[0].upper() + line[1:]
            else:
                yield line

    def do_app():
        devtools.ya.app.execute_early(do_main)(
            args,
            keep_tmp_dir=a.keep_tmp,
            diag=getattr(a, "diag", False),
            error_file=a.error_file,
            no_report=a.no_report,
            report_events=a.report_events,
            no_logs=a.no_logs,
            no_tmp_dir=a.no_tmp_dir,
            precise=a.precise,
            extra_help='\n'.join(format_help()),
        )

    stager.finish("main-processing")
    if a.profile:
        # Keep profile modules off the critical path
        try:
            import cProfile as profile
        except ImportError:
            import profile

        profile.runctx('do_app()', globals(), locals(), sort='cumulative')
    else:
        do_app()
