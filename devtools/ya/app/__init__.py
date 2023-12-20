import enum
import copy
import logging
import os
import six
import sys
import time

import core.config
import core.gsid
import core.sig_handler
import devtools.ya.core.sec as sec
import exts.os2
import exts.strings
import exts.windows
import yalibrary.app_ctx
import yalibrary.find_root
from exts.strtobool import strtobool
from yalibrary.display import build_term_display

import devtools.ya.app.modules.evlog as evlog
import devtools.ya.app.modules.params as params
import devtools.ya.app.modules.token_suppressions as token_suppressions


logger = logging.getLogger(__name__)
# mlockall is called too early, w/ no logging
MLOCK_STATUS_MESSAGE = ""


class RespawnType(enum.Enum):
    NONE = 1
    OPTIONAL = 2
    MANDATORY = 3


def is_sensitive(args):
    for arg in args:
        # skip mistyped ya args
        if arg.startswith('-'):
            continue
        return arg == 'vault'


def execute_early(action):
    # noinspection PyStatementEffect
    def helper(
        args,
        keep_tmp_dir=False,
        diag=False,
        error_file=None,
        no_report=False,
        no_logs=False,
        no_tmp_dir=False,
        precise=False,
        **kwargs
    ):
        ctx = yalibrary.app_ctx.get_app_ctx()

        modules = []

        if no_logs or is_sensitive(args):
            modules.append(('null_log', configure_null_log(ctx)))
        else:
            modules.append(('file_log', configure_file_log(ctx)))

        if not precise:
            # it is created for display not to lose some important warnings
            modules.append(
                ('display_in_memory_log', configure_in_memory_log(ctx)),
            )

        modules.extend(
            [
                ('file_in_memory_log', configure_in_memory_log(ctx, logging.DEBUG)),
                ('exit', configure_exit_interceptor(error_file)),
                ('lifecycle_ts', configure_lifecycle_timestamps()),
                # Configure `revision` before `report` module to be able to report ya version
                ('revision', configure_vcs_info()),
                ('env_checker', configure_environment_checker()),
                # Need for ya nile {udf.so} handler
                ('fetchers_storage', configure_fetchers_storage(ctx)),
            ]
        )

        if not no_tmp_dir:
            modules.append(('tmp_dir', configure_tmp_dir_interceptor(keep_tmp_dir or diag)))

        if not no_report and not is_sensitive(args):
            modules.append(('report', configure_report_interceptor(ctx)))

        if diag:
            modules.append(('diag', configure_diag_interceptor()))

        with ctx.configure(modules):
            return action(args, **kwargs)

    return helper


def execute(action, respawn=RespawnType.MANDATORY, handler_python_major_version=None, interruptable=False):
    # noinspection PyStatementEffect
    def helper(parameters):
        if handler_python_major_version:
            logger.info("Handler require python major version: %d", handler_python_major_version)

        if respawn == RespawnType.OPTIONAL:
            check_and_respawn_if_possible(handler_python_major_version)
        with_respawn = respawn == RespawnType.MANDATORY
        ctx = yalibrary.app_ctx.get_app_ctx()
        modules = [
            ('params', params.configure(parameters, with_respawn, handler_python_major_version)),
            ('hide_token', token_suppressions.configure(ctx)),
            ('state', configure_active_state(ctx, interruptable)),
            ('display', configure_display(ctx)),
            ('custom_file_log', configure_custom_file_log(ctx)),
            ('display_log', configure_display_log(ctx)),
            ('self_info', configure_self_info()),
            ('fetcher_params', configure_fetcher_params(ctx)),
            ('hide_token2', token_suppressions.configure(ctx)),
            ('fetchers_storage', configure_fetchers_storage(ctx)),
            ('fetcher', configure_fetcher(ctx)),
            ('showstack', configure_showstack()),
            ('profile', configure_profiler_support(ctx)),
            ('mlockall', configure_mlock_info()),
        ]
        if not getattr(parameters, 'no_evlogs', None) and not strtobool(os.environ.get('YA_NO_EVLOGS', '0')):
            modules.append(('evlog', evlog.configure(ctx)))

        modules.append(('dump_debug', configure_debug(ctx)))

        # XXX waiting for YA-1028
        if not getattr(parameters, 'distbuild_no_svn', False) and (
            getattr(parameters, 'need_update_dist_priority', False)
            or getattr(parameters, 'need_update_graph_gen_dist_priority', False)
        ):
            from devtools.ya.app.modules import distbs_priority

            modules.append(('distbs_priority', distbs_priority.configure(ctx)))

        with ctx.configure(modules):
            logger.debug('Run action on %s with params %s', action, params)
            report_params(ctx)
            return action(ctx.params)

    return helper


def configure_self_info():
    import app_config

    logger.debug("origin: %s", "arcadia" if app_config.in_house else "github")
    logger.debug("python: %s", sys.version_info)
    yield True


def report_params(app_ctx):
    try:
        from core.report import telemetry, ReportTypes

        params = app_ctx.params
        reportable_keys = params.as_dict()

        values_to_report = {}

        for name in reportable_keys:
            if not hasattr(params, name):
                logger.warning("Reportable parameter `%s` not found in ctx.params", name)
                continue

            values_to_report[name] = getattr(params, name)

        telemetry.report(ReportTypes.PARAMETERS, values_to_report)

        return True
    except Exception:
        logger.exception("While preparing parameters to report")

    return False


def configure_environment_checker():
    if os.environ.get('PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION') == 'python':
        sys.stderr.write(
            'Warn: Local environment contains PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python env.var. This may result in poor performance and possible errors.\n'
        )
    yield


def configure_debug(app_ctx):
    from yalibrary.debug_store import store as debug_store
    import yalibrary.debug_store

    class AppEvLogStore(debug_store.EvLogStore):
        pass

    if not getattr(app_ctx.params, 'dump_debug_enabled', True):
        AppEvLogStore.logger.debug("dump_debug disabled, will not store")
        AppEvLogStore.evlog = None
    else:
        AppEvLogStore.evlog = getattr(app_ctx, "evlog", None)

    yalibrary.debug_store.dump_debug = dump_store_obj = AppEvLogStore()

    try:
        dump_store_obj['argv'] = sys.argv
    except Exception:
        AppEvLogStore.logger.exception("While store argv")

    if AppEvLogStore.evlog is None:
        AppEvLogStore.logger.debug("evlog not found, will not store dump debug info to evlog")

        yield dump_store_obj
        return

    try:
        try:
            from library.python import svn_version

            dump_store_obj['ya_version_info'] = svn_version.svn_version()
        except Exception as e:
            AppEvLogStore.logger.debug("While store ya version: %s", e)

        try:
            tools_root = core.config.tool_root(4)
            dump_store_obj['tools_cache_root'] = tools_root
            dump_store_obj['tools_cache_db_file'] = os.path.join(tools_root, 'tcdb.sqlite')
        except Exception as e:
            AppEvLogStore.logger.debug("While store tools cache: %s", e)

        try:
            dump_store_obj['evlog_file'] = app_ctx.evlog.path
        except Exception as e:
            AppEvLogStore.logger.debug("While store evlog: %s", e)

        try:
            dump_store_obj['log_file'] = app_ctx.file_log
        except Exception as e:
            AppEvLogStore.logger.debug("While store file_log: %s", e)

        try:
            dump_store_obj['svn_revision'] = app_ctx.revision
        except Exception as e:
            AppEvLogStore.logger.debug("While store svn_revision: %s", e)

        try:
            dump_store_obj['params'] = app_ctx.params.__dict__
        except Exception as e:
            AppEvLogStore.logger.debug("While store params: %s", e)

        try:
            dump_store_obj['env'] = os.environ.copy()
        except Exception as e:
            AppEvLogStore.logger.debug("While store env: %s", e)

        try:
            from core.report import system_info

            dump_store_obj['system_info'] = system_info()
        except Exception as e:
            AppEvLogStore.logger.debug("While store system_info: %s", e)

        try:
            from core import gsid

            dump_store_obj['session_id'] = gsid.session_id()
        except Exception as e:
            AppEvLogStore.logger.debug("While store system_id: %s", e)

        try:
            from core import gsid

            dump_store_obj['cwd'] = os.getcwd()
        except Exception as e:
            AppEvLogStore.logger.debug("While store cwd: %s", e)

        yield dump_store_obj

        try:
            dump_store_obj['resources'] = _resources_report()
        except Exception as e:
            AppEvLogStore.logger.debug("While store resources: %s", e)

        try:
            dump_store_obj['finish_time'] = time.time()
        except Exception as e:
            AppEvLogStore.logger.debug("While store finish_time: %s", e)

    except SystemExit as e:
        try:
            dump_store_obj['exit_code'] = e.code
        except Exception as e:
            AppEvLogStore.logger.debug("While store exit_code: %s", e)
        raise
    finally:
        try:
            dump_store_obj['exit_code'] = 0
        except Exception as e:
            AppEvLogStore.logger.debug("While store exit_code: %s", e)


def configure_showstack():
    from yalibrary import showstack
    import signal

    if hasattr(signal, "SIGUSR1"):
        for x in showstack.configure_show_stack_on_signal(signal.SIGUSR1):
            yield x
    else:
        yield None


def configure_lifecycle_timestamps():
    fmt = "%Y-%m-%dT%H:%M:%S.%Z"
    root = logging.getLogger()

    root.debug("Start up timestamp %s (%s)", time.strftime(fmt, time.localtime()), time.strftime(fmt, time.gmtime()))
    yield
    root.debug("Exit timestamp %s (%s)", time.strftime(fmt, time.localtime()), time.strftime(fmt, time.gmtime()))


def configure_null_log(app_ctx):
    root = logging.getLogger()
    root.setLevel(logging.DEBUG)
    handler = logging.NullHandler()
    root.addHandler(handler)
    # Prevent message `No handlers could be found for logger "library.python.strings.strings"`
    # See DEVTOOLSSUPPORT-14983
    yield handler


def configure_file_log(app_ctx):
    from yalibrary.loggers import file_log

    for x in file_log.with_file_log(core.config.logs_root(), core.gsid.uid()):
        yield x


def configure_in_memory_log(app_ctx, level=None):
    from yalibrary.loggers import in_memory_log
    from core import logger

    level = level or logger.level()

    for x in in_memory_log.with_in_memory_log(level):
        yield x


def configure_custom_file_log(app_ctx):
    from yalibrary.loggers import file_log

    for x in file_log.with_custom_file_log(app_ctx, app_ctx.params, app_ctx.hide_token):
        yield x


def configure_display_log(app_ctx):
    from yalibrary.loggers import display_log
    from core import logger  # XXX

    for x in display_log.with_display_log(app_ctx, logger.level(), app_ctx.hide_token):
        yield x


def configure_profiler_support(ctx):
    from yalibrary import profiler

    return profiler.with_profiler_support(ctx)


def configure_fetcher_params(app_ctx):
    custom_fetcher = None
    fetcher_opts = getattr(app_ctx.params, 'flags', {}).get('FETCHER_OPTS')
    if fetcher_opts:
        custom_fetcher = fetcher_opts.replace('--custom-fetcher=', '').strip('"')  # XXX
    if not custom_fetcher:
        custom_fetcher = getattr(app_ctx.params, 'custom_fetcher', None)
    fetcher_params = getattr(app_ctx.params, 'fetcher_params', [])

    oauth_token = getattr(app_ctx.params, 'oauth_token', None)

    def custom_fetcher_is_top_priority():
        return custom_fetcher and fetcher_params and fetcher_params[0]['name'] == 'custom'

    if (
        not oauth_token
        and not custom_fetcher_is_top_priority()
        and getattr(app_ctx.params, 'oauth_exchange_ssh_keys', False)
        and not core.config.has_mapping()
    ):
        import app_config

        if not app_config.have_oauth_support:
            yield custom_fetcher, fetcher_params, None
            return

        import yalibrary.oauth

        oauth_token = yalibrary.oauth.resolve_token(app_ctx.params, required=False, query_passwd=False)
        if oauth_token is not None:
            oauth_token = six.ensure_str(oauth_token)

        if oauth_token and hasattr(app_ctx.params, 'oauth_token'):
            setattr(app_ctx.params, 'oauth_token', oauth_token)

    fetcher_params_strip = copy.deepcopy(fetcher_params)
    for f in fetcher_params_strip:
        if f.get('token', None):
            f['token'] = "<provided>"
    logging.debug(
        "Custom fetcher: {}, fetcher parameters: {}, sb_token {}".format(
            str(custom_fetcher), str(fetcher_params_strip), "set" if oauth_token else "none"
        )
    )
    yield custom_fetcher, fetcher_params, oauth_token


def configure_fetcher(app_ctx):
    yield app_ctx.fetchers_storage.get_default()  # in the name of legacy


def configure_fetchers_storage(app_ctx):
    import app_config
    import yalibrary.fetcher.fetchers_storage as fetchers_storage

    have_sandbox = app_config.in_house or app_config.have_sandbox_fetcher
    storage = fetchers_storage.FetchersStorage()

    try:
        app_state = app_ctx.state
    except Exception:
        app_state = None  # execute early

    if have_sandbox:
        from yalibrary.yandex.sandbox import fetcher

        try:
            custom_fetcher, fetcher_params, oauth_token = app_ctx.fetcher_params
            fetcher_instance = fetcher.SandboxFetcher(
                custom_fetcher, fetcher_params=fetcher_params, sandbox_token=oauth_token, state=app_state
            )
        except AttributeError:
            fetcher_instance = fetcher.SandboxFetcher()  # execute_early
        storage.register(fetcher_instance.ALLOWED_SCHEMAS, fetcher_instance, default=app_config.in_house)

    try:
        from yalibrary.tasklet_resources_fetcher import trs_fetcher

        fetcher_instance = trs_fetcher.TRSFetcher(state=app_state)
        storage.register(fetcher_instance.ALLOWED_SCHEMAS, fetcher_instance)
    except ImportError:
        pass

    if not app_config.in_house:
        storage.register(['sbr'], None)  # for opensource ya

    yield storage


def configure_display(app_ctx):
    import yalibrary.display as yadisplay
    from yalibrary import formatter

    fmt = formatter.new_formatter(
        exts.os2.is_tty(),
        profile=getattr(app_ctx.params, 'terminal_profile', None),
        teamcity=getattr(app_ctx.params, 'teamcity', False),
        show_status=getattr(app_ctx.params, 'do_emit_status', True),
    )
    term_display = yadisplay.Display(sys.stderr, fmt)

    try:
        if getattr(app_ctx.params, 'html_display', None):
            with open(app_ctx.params.html_display, 'w') as html_f:
                scheme = formatter.palette.LIGHT_HTML_SCHEME
                profile = {formatter.Highlight.PATH: formatter.Colors.BLUE}
                html_display = yadisplay.Display(
                    html_f,
                    formatter.Formatter(formatter.HtmlSupport(scheme=scheme, profile=profile), show_status=False),
                )
                try:
                    yield yadisplay.CompositeDisplay(term_display, html_display)
                finally:
                    html_display.close()
        else:
            yield term_display
    finally:
        term_display.close()


def configure_active_state(app_ctx, interruptable):
    import signal
    import yalibrary.active_state

    state = yalibrary.active_state.ActiveState(__name__)

    if interruptable:
        signal.signal(signal.SIGINT, core.sig_handler.instant_sigint_exit_handler)
    else:
        if os.environ.get('Y_FAST_CANCEL', 'no') != 'yes':
            signal.signal(signal.SIGINT, lambda _, __: state.stopping())

    signal.signal(signal.SIGTERM, lambda _, __: state.stopping())

    try:
        yield state
    finally:
        state.stop()


def configure_vcs_info():
    import logging

    revision = None
    try:
        from library.python import svn_version

        revision = svn_version.svn_revision()
        logging.debug('Release revision: %s', revision)
    except ImportError:
        pass
    yield revision


def configure_mlock_info():
    logger.debug(MLOCK_STATUS_MESSAGE)
    yield


def check_and_respawn_if_possible(handler_python_major_version=None):
    arcadia_root = yalibrary.find_root.detect_root(os.getcwd())
    logger.debug("Cwd arcadia root: {}".format(arcadia_root))
    if arcadia_root is not None:
        import core.respawn

        core.respawn.check_for_respawn(arcadia_root, handler_python_major_version)


def _resources_report():
    import resource

    return {
        'max_rss': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024,
        'utime': resource.getrusage(resource.RUSAGE_SELF).ru_utime,
        'stime': resource.getrusage(resource.RUSAGE_SELF).ru_stime,
        'inblock': resource.getrusage(resource.RUSAGE_SELF).ru_inblock,
        'oublock': resource.getrusage(resource.RUSAGE_SELF).ru_oublock,
    }


def configure_report_interceptor(ctx):
    # we can only do that after respawn with valid python
    from core.report import telemetry, ReportTypes

    telemetry.init_reporter(suppressions=sec.mine_suppression_filter())

    telemetry.report(
        ReportTypes.EXECUTION,
        {
            'cmd_args': sec.mine_cmd_args(),
            'env_vars': sec.mine_env_vars(),
            'cwd': os.getcwd(),
            '__file__': __file__,
            'version': ctx.revision,
        },
    )

    start = time.time()
    success = False
    exit_code = -1
    try:
        yield
    except KeyboardInterrupt:
        raise
    except SystemExit as e:
        success = not e.code  # None or 0 are OK codes
        exit_code = e.code or 0
        raise
    except Exception:
        import traceback

        err = sys.exc_info()[1]
        telemetry.report(
            ReportTypes.FAILURE,
            {
                'cmd_args': sec.mine_cmd_args(),
                'env_vars': sec.mine_env_vars(),
                'cwd': os.getcwd(),
                '__file__': __file__,
                'traceback': traceback.format_exc(),
                'type': sys.exc_info()[0].__name__,
                'prefix': core.yarg.OptsHandler.latest_handled_prefix(),
                'mute': getattr(err, 'mute', None),
                'retriable': getattr(err, 'retriable', None),
                'version': ctx.revision,
            },
        )
        raise
    finally:
        duration = time.time() - start

        telemetry.report(
            ReportTypes.TIMEIT,
            dict(
                cwd=os.getcwd(),
                duration=duration,
                cmd_args=sec.mine_cmd_args(),
                success=success,
                exit_code=exit_code,
                prefix=core.yarg.OptsHandler.latest_handled_prefix(),
                version=ctx.revision,
                **_resources_report()
            ),
        )


def configure_tmp_dir_interceptor(keep_tmp_dir):
    from exts import tmp

    try:
        tmp.set_tmp_dir(core.config.tmp_path(), keep_dir=keep_tmp_dir)
    except OSError as e:
        import errno

        if e.errno == errno.ENOSPC:
            sys.stderr.write("No space left on device, clean up some files. Try running 'ya gc cache'\n")
        else:
            raise e

    try:
        yield
    finally:
        tmp.remove_tmp_dirs()


def configure_diag_interceptor():
    import yalibrary.diagnostics as diag

    diag.init_diagnostics()
    try:
        yield
    finally:
        url = diag.finish_diagnostics()
        from core.report import telemetry, ReportTypes

        telemetry.report(
            ReportTypes.DIAGNOSTICS,
            {'url': url},
        )


def configure_exit_interceptor(error_file):
    def print_message(exc):
        error_message = exc.message if hasattr(exc, "message") else str(exc)
        logger.debug(error_message)
        display = build_term_display(sys.stderr, exts.os2.is_tty())  # XXX
        display.emit_message('[[bad]]' + error_message + '[[rst]]')

    try:
        yield
        sys.exit(0)
    except Exception as e:
        from core import error

        if exts.windows.on_win() and isinstance(e, WindowsError):
            # Transcode system Windows errors to utf-8
            from exts.windows import transcode_error

            transcode_error(e)

        temp_error = error.is_temporary_error(e)
        mute_error = getattr(e, 'mute', False)
        retriable_error = getattr(e, 'retriable', True)

        if mute_error:
            print_message(e)
        else:
            import traceback

            exc_str = six.ensure_str(
                traceback.format_exc(),
                encoding=exts.strings.get_stream_encoding(sys.stderr),
            )
            sys.stderr.write(exc_str)

        no_space_left = False
        if isinstance(e, OSError):
            import errno

            if e.errno == errno.ENOSPC:
                no_space_left = True

        if no_space_left:
            sys.stderr.write(
                "Seems like you have no space left on device. \n"
                "You can try: \n"
                "* run `ya --no-logs gc cache`\n"
                "* remove some `~/.ya` subdirectories\n"
                "* ask https://st.yandex-team.ru/createTicket?queue=DEVTOOLSSUPPORT&_form=65090\n"
            )

        if error_file:
            if not no_space_left:
                with open(error_file, 'w') as ef:
                    import traceback

                    ef.write('Error:\n{}\n\n{}'.format(repr(e), traceback.format_exc()))
            else:
                sys.stderr.write("Can't write error into file")

        import app_config

        if app_config.in_house and sys.version_info.major == 3:
            sys.stderr.write(
                "If you have some troubles with ya-bin3, you can disable "
                "all py3-handlers with `YA_DISABLE_PY3_HANDLERS` environment "
                "or you can specify python version with `ya -2 ...` flag\n"
            )

        error_code = 1
        if not retriable_error:
            error_code = core.error.ExitCodes.NOT_RETRIABLE_ERROR
        elif temp_error:
            error_code = core.error.ExitCodes.INFRASTRUCTURE_ERROR
        sys.exit(error_code)
