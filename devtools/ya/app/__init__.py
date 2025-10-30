import enum
import copy
import logging
import os
import re
import six
import sys
import time
import typing  # noqa: F401

import app_config
import devtools.ya.core.error as core_error
import devtools.ya.core.config
import devtools.ya.core.gsid
import devtools.ya.core.sig_handler
import devtools.ya.core.stage_tracer as stage_tracer
import devtools.ya.core.stage_aggregator as stage_aggregator
import devtools.ya.core.event_handling as event_handling
import devtools.ya.core.monitoring as monitoring
import devtools.ya.core.sec as sec
import devtools.ya.core.user as user
import devtools.ya.core.yarg
import exts.asyncthread
import exts.os2
import exts.strings
import exts.windows
import devtools.ya.yalibrary.app_ctx
import yalibrary.find_root
import yalibrary.vcs as vcs
from exts.strtobool import strtobool
from yalibrary.display import build_term_display
from yalibrary.vcs import vcsversion

from .modules import evlog
from .modules import params
from .modules import token_suppressions


logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer(stage_tracer.StagerGroups.OVERALL_EXECUTION)
modules_stager = stage_tracer.get_tracer(stage_tracer.StagerGroups.MODULE_LIFECYCLE)
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
        report_events=None,
        no_logs=False,
        no_tmp_dir=False,
        precise=False,
        **kwargs,
    ):
        modules_initialization_early_stage = stager.start("modules-initialization-early")
        ctx = devtools.ya.yalibrary.app_ctx.get_app_ctx()

        fill_tracer_with_environ_stages()

        modules = []

        modules.append(('uid', configure_uid()))
        modules.append(('username', configure_user_heuristic()))

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
                # Configure `revision` before `report` module to be able to report ya version
                ('revision', configure_vcs_info()),
                ('vcs_type', configure_vcs_type()),
                ('exit', configure_exit_interceptor(error_file)),
                ('handler_info', configure_handler_info()),
            ]
        )

        if not is_sensitive(args):
            modules.extend(
                [
                    ('metrics_reporter', configure_metrics_reporter(ctx)),
                    ('report', configure_report_interceptor(ctx, report_events if no_report else 'all')),
                ]
            )

        modules.append(
            ('exit_code', configure_exit_code_definition()),
        )

        modules.extend(
            [
                ('lifecycle_ts', configure_lifecycle_timestamps()),
                ('env_checker', configure_environment_checker()),
                # Need for ya nile {udf.so} handler
                ('legacy_sandbox_fetcher', configure_legacy_sandbox_fetcher(ctx)),
            ]
        )

        if not no_tmp_dir:
            modules.append(('tmp_dir', configure_tmp_dir_interceptor(keep_tmp_dir or diag)))

        if diag:
            modules.append(('diag', configure_diag_interceptor()))

        with ctx.configure(modules, modules_stager):
            modules_initialization_early_stage.finish()
            return action(args, **kwargs)

    return helper


def execute(action, respawn=RespawnType.MANDATORY):
    # noinspection PyStatementEffect
    def helper(parameters, **kwargs):
        stager.finish("handler-selection")
        modules_initialization_full_stage = stager.start("modules-initialization-full")

        if respawn == RespawnType.OPTIONAL:
            check_and_respawn_if_possible()
        with_respawn = respawn == RespawnType.MANDATORY
        ctx = devtools.ya.yalibrary.app_ctx.get_app_ctx()
        modules = [
            ('params', params.configure(parameters, with_respawn)),
            ('hide_token', token_suppressions.configure(ctx)),
            ('state', configure_active_state(ctx)),
            ('display', configure_display(ctx)),
            ('custom_file_log', configure_custom_file_log(ctx)),
            ('display_log', configure_display_log(ctx)),
            ('vcs_type', configure_vcs_type(ctx)),
            ('self_info', configure_self_info()),
            ('fetcher_params', configure_fetcher_params(ctx)),
            # TODO: kuzmich321@ (ufetcher) rm when migrated
            ('use_universal_fetcher_everywhere', configure_use_universal_fetcher_everywhere(ctx)),
            ('docker_config_path', configure_docker_config_path(ctx)),
            ('hide_token2', token_suppressions.configure(ctx)),
            ('legacy_sandbox_fetcher', configure_legacy_sandbox_fetcher(ctx)),
            ('showstack', configure_showstack()),
            ('profile', configure_profiler_support(ctx)),
            ('mlockall', configure_mlock_info()),
            ('event_queue', configure_event_queue()),
            ('changelist_store', configure_changelist_store(ctx)),
        ]
        if not getattr(parameters, 'no_evlogs', None) and not strtobool(os.environ.get('YA_NO_EVLOGS', '0')):
            modules.append(('evlog', evlog.configure(ctx)))

        modules.append(('dump_debug', configure_debug(ctx)))
        modules.append(('fast_vcs_info_json_callback', configure_fast_vcs_info_json(ctx)))

        with ctx.configure(modules, modules_stager):
            el = getattr(ctx, "evlog", None)
            if el:
                stage_tracer.stage_tracer.add_consumer(stage_tracer.EvLogConsumer(el))
            logger.debug('Run action on %s with params %s', action, ctx.params)
            report_params(ctx)
            action_name = getattr(action, "__name__", "module")
            modules_initialization_full_stage.finish()

            with stager.scope("invoke-{}".format(action_name)):
                return action(ctx.params)

    return helper


def configure_self_info():
    logger.debug("origin: %s", "arcadia" if app_config.in_house else "github")
    logger.debug("python: %s", sys.version_info)
    yield True


def _get_bootstrap_intervals():
    aggregator = stage_aggregator.BootstrapAggregator()
    start, end = time.time(), 0
    events = list(aggregator.applicable_events(stage_tracer.get_stat(stage_tracer.StagerGroups.OVERALL_EXECUTION)))
    if len(events) == 0:
        return None, None

    for stat in events:
        start = min(stat[0], start)
        end = max(stat[1], end)

    # hack to display stage correctly in evlog viewer
    start -= 1e-4
    return start, end


def fill_tracer_with_environ_stages():
    last_environ_stage_tstamp = time.time()
    stages = os.environ.pop('YA_STAGES', '')
    if stages:
        prev_stage = None
        for stage in stages.split(":"):
            name, tstamp = stage.split("@")
            tstamp = float(tstamp)
            if prev_stage is not None:
                prev_stage.finish(tstamp)
            prev_stage = stager.start(name, tstamp)

        if prev_stage is not None:
            prev_stage.finish(last_environ_stage_tstamp)

    start, end = _get_bootstrap_intervals()
    if start is not None:
        bootstrap = stager.start("ya-bootstrap", start)
        bootstrap.finish(end)


def aggregate_stages():
    aggregated_stages = {}
    try:
        stages = stage_tracer.get_stat(stage_tracer.StagerGroups.OVERALL_EXECUTION)
        for aggregator in stage_aggregator.get_aggregators():
            aggregated_stages.update(aggregator.aggregate(stages))

        stages = stage_tracer.get_stat(stage_tracer.StagerGroups.MODULE_LIFECYCLE)
        aggregator = stage_aggregator.ModuleLifecycleAggregator()
        aggregated_stages.update(aggregator.aggregate(stages))

        stages = stage_tracer.get_stat(stage_tracer.StagerGroups.CHANGELIST_OPERATIONS)
        aggregator = stage_aggregator.ArcOperationsAggregator()
        aggregated_stages.update(aggregator.aggregate(stages))
    except Exception:
        logger.exception("While aggregating stages")

    return aggregated_stages


def report_params(app_ctx):
    try:
        from devtools.ya.core.report import telemetry, ReportTypes

        params = app_ctx.params
        reportable_keys = params.as_dict()

        values_to_report = {}

        for name in reportable_keys:
            if not hasattr(params, name):
                logger.warning("Reportable parameter `%s` not found in ctx.params", name)
                continue

            values_to_report[name] = getattr(params, name)

        telemetry.report(ReportTypes.PARAMETERS, values_to_report)

    except Exception:
        logger.exception("While preparing parameters to report")


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
            tools_root = devtools.ya.core.config.tool_root(4)
            dump_store_obj['tools_cache_root'] = tools_root
        except Exception as e:
            AppEvLogStore.logger.debug("While store tools cache: %s", e)

        try:
            dump_store_obj['evlog_file'] = app_ctx.evlog.filepath
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
            from devtools.ya.core.report import system_info

            dump_store_obj['system_info'] = system_info()
        except Exception as e:
            AppEvLogStore.logger.debug("While store system_info: %s", e)

        try:
            from devtools.ya.core import gsid

            dump_store_obj['session_id'] = gsid.session_id()
        except Exception as e:
            AppEvLogStore.logger.debug("While store system_id: %s", e)

        try:
            from devtools.ya.core import gsid

            dump_store_obj['cwd'] = os.getcwd()
        except Exception as e:
            AppEvLogStore.logger.debug("While store cwd: %s", e)

        try:
            dump_store_obj['changelist_store'] = app_ctx.changelist_store
        except Exception as e:
            AppEvLogStore.logger.debug("While store changelist_store: %s", e)

        yield dump_store_obj

        try:
            dump_store_obj['resources'] = _resources_report()
        except Exception as e:
            AppEvLogStore.logger.debug("While store resources: %s", e)

        try:
            dump_store_obj['finish_time'] = time.time()
        except Exception as e:
            AppEvLogStore.logger.debug("While store finish_time: %s", e)

        try:
            dump_store_obj['handler'] = app_ctx.handler_info["handler"]
        except Exception as e:
            AppEvLogStore.logger.debug("While store handler: %s", e)

        try:
            dump_store_obj['cache_dir'] = devtools.ya.core.config.misc_root()
        except Exception as e:
            AppEvLogStore.logger.debug("While store cache_dir: %s", e)

    except SystemExit as e:
        try:
            dump_store_obj['exit_code'] = e.code
        except Exception as e:
            AppEvLogStore.logger.debug("While store exit_code: %s", e)
        raise
    else:
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


def configure_uid():
    yield devtools.ya.core.gsid.uid()


def configure_user_heuristic():
    yield user.get_user()


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

    for x in file_log.with_file_log(devtools.ya.core.config.logs_root(), app_ctx.uid):
        yield x


def configure_changelist_store(app_ctx):
    from devtools.ya.yalibrary.build_graph_cache import changelist_storage

    if getattr(app_ctx.params, "build_graph_cache_force_local_cl", False):
        for x in changelist_storage.with_changelist_storage(devtools.ya.core.config.logs_root(), app_ctx.uid):
            yield x
    else:
        yield


def configure_in_memory_log(app_ctx, level=None):
    from yalibrary.loggers import in_memory_log
    from devtools.ya.core import logger

    level = level or logger.level()

    for x in in_memory_log.with_in_memory_log(level):
        yield x


def configure_custom_file_log(app_ctx):
    from yalibrary.loggers import file_log

    for x in file_log.with_custom_file_log(app_ctx, app_ctx.params, app_ctx.hide_token):
        yield x


def configure_display_log(app_ctx):
    from yalibrary.loggers import display_log
    from devtools.ya.core import logger  # XXX

    for x in display_log.with_display_log(app_ctx, logger.level(), app_ctx.hide_token):
        yield x


def configure_profiler_support(ctx):
    from yalibrary import profiler

    return profiler.with_profiler_support(ctx)


# TODO: kuzmich321@ (ufetcher) remove when full migration to universal fetcher completed
def configure_use_universal_fetcher_everywhere(app_ctx):
    should_use = getattr(app_ctx.params, 'use_universal_fetcher_everywhere', False)
    yield should_use


def configure_docker_config_path(app_ctx):
    user_config_path = getattr(app_ctx.params, 'docker_config_path', None)
    yield user_config_path or ""


def configure_fetcher_params(app_ctx):
    custom_fetcher = None
    fetcher_opts = getattr(app_ctx.params, 'flags', {}).get('FETCHER_OPTS')
    if fetcher_opts:
        custom_fetcher = fetcher_opts.replace('--custom-fetcher=', '').strip('"')  # XXX
    if not custom_fetcher:
        custom_fetcher = getattr(app_ctx.params, 'custom_fetcher', None)
    fetcher_params = getattr(app_ctx.params, 'fetcher_params', [])

    oauth_token = getattr(app_ctx.params, 'sandbox_oauth_token', None) or getattr(app_ctx.params, 'oauth_token', None)

    def custom_fetcher_is_top_priority():
        return custom_fetcher and fetcher_params and fetcher_params[0]['name'] == 'custom'

    if (
        not oauth_token
        and not custom_fetcher_is_top_priority()
        and getattr(app_ctx.params, 'oauth_exchange_ssh_keys', False)
        and not devtools.ya.core.config.has_mapping()
    ):
        if not app_config.have_oauth_support:
            yield custom_fetcher, fetcher_params, None
            return

        import yalibrary.oauth

        oauth_token = yalibrary.oauth.resolve_token(app_ctx.params, required=False, query_passwd=False)
        if oauth_token is not None:
            oauth_token = six.ensure_str(oauth_token)

        if oauth_token:
            if hasattr(app_ctx.params, 'oauth_token'):
                setattr(app_ctx.params, 'oauth_token', oauth_token)
            if hasattr(app_ctx.params, 'sandbox_oauth_token'):
                setattr(app_ctx.params, 'sandbox_oauth_token', oauth_token)

    fetcher_params_strip = copy.deepcopy(fetcher_params)
    for f in fetcher_params_strip:
        if f.get('token', None):
            f['token'] = "<provided>"
    logger.debug(
        "Custom fetcher: {}, fetcher parameters: {}, sb_token {}".format(
            str(custom_fetcher), str(fetcher_params_strip), "set" if oauth_token else "none"
        )
    )
    yield custom_fetcher, fetcher_params, oauth_token


# TODO: kuzmich321@ (ufetcher) remove when full migration to universal fetcher completed
def configure_legacy_sandbox_fetcher(app_ctx):
    if app_config.in_house:
        try:
            app_state = app_ctx.state
        except Exception:
            app_state = None  # execute early

        from devtools.ya.yalibrary.yandex.sandbox import fetcher

        if hasattr(app_ctx, 'fetcher_params'):
            custom_fetcher, fetcher_params, oauth_token = app_ctx.fetcher_params

            yield fetcher.SandboxFetcher(
                custom_fetcher, fetcher_params=fetcher_params, sandbox_token=oauth_token, state=app_state
            )
        else:
            yield fetcher.SandboxFetcher()
    else:
        yield None


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


def configure_active_state(app_ctx):
    import signal
    import devtools.ya.yalibrary.active_state

    state = devtools.ya.yalibrary.active_state.ActiveState(__name__)
    sigint_exit_handler = devtools.ya.core.sig_handler.create_sigint_exit_handler()

    signal.signal(signal.SIGINT, sigint_exit_handler)
    signal.signal(signal.SIGTERM, lambda _, __: state.stopping())

    try:
        yield state
    finally:
        state.stop()


def configure_vcs_info():
    revision = None

    try:
        from library.python import svn_version

        revision = svn_version.svn_revision()
        logger.debug('Release revision: %s', revision)
        if revision == -1:
            logger.debug("Release branch: %s", svn_version.svn_branch())
            logger.debug("Release hash: %s", svn_version.hash())
            logger.debug("Release commit_id: %s", svn_version.commit_id())
            logger.debug("Release timestamp: %s", svn_version.svn_timestamp())

    except ImportError:
        pass

    yield revision


def configure_fast_vcs_info_json(ctx):
    # type: (devtools.ya.yalibrary.app_ctx.AppCtx) -> typing.Generator[typing.Callable[[], dict], None, None]
    arc_root = getattr(ctx.params, 'arc_root', None)
    if not arc_root:
        arc_root = devtools.ya.core.config.find_root(fail_on_error=False)
    if not arc_root:
        arc_root = os.getcwd()
        logger.debug('Unable to get vcs root for %s', os.getcwd())

    result = exts.asyncthread.future(lambda: _load_fast_vcs_info(arc_root))

    yield result


def _load_fast_vcs_info(arc_root):
    # type: (str) -> dict
    from devtools.ya.core.report import telemetry, ReportTypes

    result = vcsversion.get_fast_version_info(arc_root, timeout=5)
    telemetry.report(ReportTypes.FAST_VCS_INFO_JSON, result)

    return result


def configure_vcs_type(ctx=None):
    if ctx is None:
        cwd = os.getcwd()
    else:
        cwd = getattr(ctx.params, 'arc_root', os.getcwd())

    vcs_type = vcs.detect_vcs_type(cwd=cwd)
    logging.debug('vcs type: %s', vcs_type)
    yield vcs_type


def configure_mlock_info():
    logger.debug(MLOCK_STATUS_MESSAGE)
    yield


def configure_event_queue():
    queue = event_handling.EventQueue()
    yield queue


def check_and_respawn_if_possible():
    arcadia_root = yalibrary.find_root.detect_root(os.getcwd())
    logger.debug("Cwd arcadia root: {}".format(arcadia_root))
    if arcadia_root is not None:
        import devtools.ya.core.respawn

        devtools.ya.core.respawn.check_for_respawn(arcadia_root)


def _ya_downloads_report():
    stages = stage_tracer.get_stat(stage_tracer.StagerGroups.OVERALL_EXECUTION)
    aggregator = stage_aggregator.YaScriptDownloadsAggregator()
    return aggregator.aggregate(stages)


def _vmhwm():
    if not sys.platform.startswith("linux"):
        return

    line = None
    status_file = "/proc/self/status"
    with open(status_file) as f:
        for s in f:
            if s.startswith("VmHWM:"):
                line = s
                break
    if not line:
        logger.debug("VmHWM not found in %s", status_file)
        return

    # JFI: 'kB' suffix is hardcoded in the Linux kernel source file fs/proc/task_mmu.c
    if m := re.match(r"VmHWM:\s*(\d+)\s*kB", line):
        return int(m.group(1)) * 1024
    else:
        logger.debug("Unexpected VmHWM status string: '%s'", line)


def _resources_report():
    try:
        import resource
    except ImportError:
        return {}

    stat = {
        'max_rss': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024,
        'utime': resource.getrusage(resource.RUSAGE_SELF).ru_utime,
        'stime': resource.getrusage(resource.RUSAGE_SELF).ru_stime,
        'inblock': resource.getrusage(resource.RUSAGE_SELF).ru_inblock,
        'oublock': resource.getrusage(resource.RUSAGE_SELF).ru_oublock,
    }
    if vmhwm := _vmhwm():
        stat["vmhwm_bytes"] = vmhwm
    return stat


def configure_report_interceptor(ctx, report_events):
    # we can only do that after respawn with valid python
    from devtools.ya.core.report import telemetry, ReportTypes, mine_env_vars, mine_cmd_args, parse_events_filter

    params_dict = ctx.params.__dict__ if hasattr(ctx, "params") else None

    telemetry.init_reporter(
        suppressions=sec.mine_suppression_filter(params_dict),
        report_events=parse_events_filter.parse_events_filter(report_events),
    )

    telemetry.report(
        ReportTypes.EXECUTION,
        {
            'cmd_args': mine_cmd_args(),
            'env_vars': mine_env_vars(),
            'cwd': os.getcwd(),
            '__file__': __file__,
            'version': ctx.revision,
            'vcs_type': ctx.vcs_type,
        },
    )
    ctx.metrics_reporter.report_metric(
        monitoring.MetricNames.YA_STARTED,
        urgent=True,
    )

    start = time.time()
    for stat in stage_tracer.get_stat(stage_tracer.StagerGroups.OVERALL_EXECUTION).values():
        for intvl in stat.intervals:
            start = min(intvl[0], start)

    success = False
    exit_code = 0
    exception_name = ""
    exception_muted = False
    try:
        yield
    except BaseException as e:
        if isinstance(e, KeyboardInterrupt):
            exit_code = -1
            raise
        elif isinstance(e, SystemExit):
            success = not e.code  # None or 0 are OK codes
            exit_code = e.code or 0
            if exit_code == 0:
                raise
        elif isinstance(e, Exception):
            exit_code = getattr(e, 'ya_exit_code', None) or -1
            success = not exit_code  # only 0 is ok
        else:
            raise

        import traceback

        e.ya_exit_code = exit_code

        prefix = devtools.ya.core.yarg.OptsHandler.latest_handled_prefix()
        exception_name = e.__class__.__name__
        exception_muted = getattr(e, 'mute', False)
        telemetry.report(
            ReportTypes.FAILURE,
            {
                'cmd_args': mine_cmd_args(),
                'env_vars': mine_env_vars(),
                'cwd': os.getcwd(),
                '__file__': __file__,
                'traceback': traceback.format_exc(),
                'type': exception_name,
                'prefix': prefix,
                'mute': exception_muted,
                'retriable': getattr(e, 'retriable', None),
                'version': ctx.revision,
            },
        )
        raise
    finally:
        duration = time.time() - start

        additional_fields = _resources_report()
        additional_fields.update(_ya_downloads_report())

        prefix = devtools.ya.core.yarg.OptsHandler.latest_handled_prefix()
        telemetry.report(
            ReportTypes.TIMEIT,
            dict(
                cwd=os.getcwd(),
                duration=duration,
                start_time=start,
                cmd_args=mine_cmd_args(),
                success=success,
                exit_code=exit_code,
                prefix=prefix,
                version=ctx.revision,
                total_walltimes=aggregate_stages(),
                **additional_fields,
            ),
        )

        telemetry.report(
            ReportTypes.FINISH,
            telemetry.compose(
                ReportTypes.TIMEIT,
                ReportTypes.BUILD_ERRORS,
                ReportTypes.PROFILE_BY_TYPE,
                ReportTypes.VCS_INFO,
                ReportTypes.FAST_VCS_INFO_JSON,
            ),
        )

        ctx.metrics_reporter.report_metric(
            monitoring.MetricNames.YA_FINISHED,
            labels={
                "handler": prefix[1] if len(prefix) > 1 else "undefined",
                "prefix": " ".join(prefix),
                "exit_code": exit_code,
                "exc_info": exception_name,
                "mute": exception_muted,
            },
            urgent=True,
        )
        telemetry.stop_reporter()  # flush urgent reports


def configure_metrics_reporter(ctx):
    from devtools.ya.core.report import telemetry, compact_system_info

    metrics_reporter = monitoring.MetricStore(
        {
            'platform': compact_system_info(),
            'version': ctx.revision,
            'userclass': user.classify_user(ctx.username),
        },
        telemetry,
    )

    yield metrics_reporter


def configure_tmp_dir_interceptor(keep_tmp_dir):
    from exts import tmp

    try:
        tmp.set_tmp_dir(devtools.ya.core.config.tmp_path(), keep_dir=keep_tmp_dir)
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
        from devtools.ya.core.report import telemetry, ReportTypes

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
        if exts.windows.on_win() and isinstance(e, WindowsError):
            # Transcode system Windows errors to utf-8
            from exts.windows import transcode_error

            transcode_error(e)

        mute_error = getattr(e, 'mute', False)

        if mute_error:
            print_message(e)
        else:
            import traceback

            exc_str = six.ensure_str(
                traceback.format_exc(),
                encoding=exts.strings.get_stream_encoding(sys.stderr),
            )
            sys.stderr.write(exc_str)

            if app_config.in_house:
                sys.stderr.write(
                    "If you have problems with ya, you can submit a request to our support: {}\n".format(
                        app_config.support_url
                    )
                )

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
                "* remove some `~/.ya/` subdirectories\n"
            )
            if app_config.in_house:
                sys.stderr.write("* ask  for help: {}\n".format(app_config.support_url))

        if error_file:
            if not no_space_left:
                with open(error_file, 'w') as ef:
                    import traceback

                    ef.write('Error:\n{}\n\n{}'.format(repr(e), traceback.format_exc()))
            else:
                sys.stderr.write("Can't write error into file")

        sys.exit(e.ya_exit_code)


def configure_exit_code_definition():
    try:
        yield
    except Exception as e:
        temp_error = core_error.is_temporary_error(e)
        mute_error = getattr(e, 'mute', False)
        retriable_error = getattr(e, 'retriable', True)

        if mute_error:
            if getattr(e, 'exit_code', None) is not None:
                error_code = getattr(e, 'exit_code')
            else:
                error_code = core_error.ExitCodes.GENERIC_ERROR
        else:
            error_code = core_error.ExitCodes.UNHANDLED_EXCEPTION

        if not retriable_error:
            error_code = core_error.ExitCodes.NOT_RETRIABLE_ERROR
        elif temp_error:
            error_code = core_error.ExitCodes.INFRASTRUCTURE_ERROR

        logger.debug("Derived exit code is %s", error_code)

        e.ya_exit_code = error_code
        raise


def configure_handler_info():
    yield {}
