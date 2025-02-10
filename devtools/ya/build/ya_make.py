import collections
import copy
import glob
import logging
import os
import subprocess
import sys
import tempfile
import time
import typing as tp  # noqa
from devtools.ya.test import const

import six
import pickle as cPickle

import app_config
import devtools.ya.build.build_plan as bp
import devtools.ya.build.build_result as br
import devtools.ya.build.gen_plan as gp
import devtools.ya.build.graph as lg
import devtools.ya.build.owners as ow
import devtools.ya.build.reports.autocheck_report as ar
import devtools.ya.build.reports.results_listener as pr
import devtools.ya.build.stat.graph_metrics as st
import devtools.ya.build.stat.statistics as bs
import devtools.ya.core.config as core_config
import devtools.ya.core.error
import devtools.ya.core.event_handling as event_handling
import devtools.ya.core.profiler as cp
import devtools.ya.core.report
import devtools.ya.core.yarg
import exts.asyncthread as core_async
import exts.filelock
import exts.fs
import exts.hashing as hashing
import exts.os2
import exts.path2
import exts.timer
import exts.tmp
import exts.windows
import exts.yjson as json
import devtools.ya.test.util.tools as test_tools
from devtools.ya.build import build_facade
from devtools.ya.build import frepkage, test_results_console_printer
from devtools.ya.build.evlog.progress import (
    get_print_status_func,
    YmakeTimeStatistic,
)
from devtools.ya.build.reports import build_reports as build_report
from devtools.ya.build.reports import configure_error as ce
from devtools.ya.build.reports import results_report
from devtools.ya.core import stage_tracer
from devtools.ya.yalibrary import sjson
from exts import func
from exts.decompress import udopen
from exts.compress import zcopen
from yalibrary import tools
from yalibrary.last_failed import last_failed
from yalibrary.runner import patterns as ptrn
from yalibrary.runner import result_store
from yalibrary.runner import ring_store
from yalibrary.runner import uid_store
from yalibrary.toolscache import (
    toolscache_version,
    get_task_stats,
    release_all_data,
    post_local_cache_report,
    tc_force_gc,
)
from devtools.ya.build.cache_kind import CacheKind
from devtools.ya.yalibrary.yandex.distbuild import distbs_consts

try:
    from yalibrary import build_graph_cache
except ImportError:
    build_graph_cache = None


CACHE_GENERATION = '2'

ARC_PREFIX = 'arcadia/'
ATD_PREFIX = 'arcadia_tests_data/'

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("ya_make")


class ConfigurationError(Exception):
    mute = True

    def __init__(self, msg=None):
        super().__init__(msg or 'Configure error (use -k to proceed)')


class EmptyProfilesListException(Exception):
    mute = True


def match_urls_and_outputs(urls, outputs):
    def norm(p):
        return os.path.normpath(p).replace('$(BUILD_ROOT)/', '')

    result = {}
    urls = {(norm(u), u) for u in urls}
    assert len(urls) == len(outputs), (urls, outputs)
    norm_outs = [(norm(o), o) for o in outputs]
    for norm_out, out in sorted(norm_outs, key=lambda oo: len(oo[1]), reverse=True):
        for norm_url, url in urls:
            if norm_url.endswith(norm_out):
                result[url] = out
                urls.remove((norm_url, url))
                break

    assert len(urls) == 0, urls
    return result


def remove_safe(p):
    try:
        exts.fs.remove_tree(p)
    except OSError as e:
        logger.debug('in remove_safe(): %s', e)


def normalize_by_dir(dirs, dir_):
    res = []

    for d in dirs:
        if exts.path2.path_startswith(d, dir_):
            res.append(dir_)

        else:
            res.append(d)

    return res


class DisplayMessageSubscriber(event_handling.SubscriberSpecifiedTopics):
    topics = {"NEvent.TDisplayMessage"}

    def _should_print(self, msg):
        # type: (dict) -> bool
        if self._opts.be_verbose:
            return True

        if 'noauto' in msg:
            if msg.strip().endswith('.py'):
                return False

        return True

    def __init__(self, opts, display, printed=None):
        # type: (tp.Any, tp.Any, tp.Any | None) -> None
        self._opts = opts
        self._display = display
        self._printed = printed or set()

    def _action(self, event):
        # type: (dict) -> None
        severity = event['Type']
        sub = '[' + event['Sub'] + ']' if event['Sub'] else ''
        data = event['Message']

        data = six.ensure_str(data)

        where = 'in [[imp]]{}[[rst]]'.format(event['Where']) if 'Where' in event else ''
        if len(where):
            where += ':{}:{}: '.format(event['Row'], event['Column']) if 'Row' in event and 'Column' in event else ': '
        platform = '{{{}}} '.format(event['Platform']) if 'Platform' in event else ''

        msg = '{}[[{}]]{}{}[[rst]]: {}{}'.format(platform, event['Mod'], severity, sub, where, data)
        if msg not in self._printed and (self._opts.be_verbose or severity != 'Debug'):
            self._printed.add(msg)

            if self._should_print(msg):
                self._display.emit_message(msg)


class YmakeEvlogSubscriber(event_handling.SubscriberLoggable):
    def __init__(
        self, evlog_writer  # type: tp.Any
    ):
        self._evlog_writer = evlog_writer

    def _action(self, event):
        # type: (dict) -> None
        self._evlog_writer(event['_typename'], **event)


class PrintMessageSubscriber(event_handling.SubscriberLoggable):
    def __init__(self):
        self.errors = collections.defaultdict(set)

    def _action(self, event):
        # type: (dict) -> None
        if event['_typename'] == 'NEvent.TDisplayMessage' and event['Type'] == 'Error' and 'Where' in event:
            platform = '{{{}}}: '.format(event['Platform']) if 'Platform' in event else ''
            self.errors[event['Where']].add(
                ce.ConfigureError(platform + event['Message'].strip(), event.get('Row', 0), event.get('Column', 0))
            )
        elif event['_typename'] == 'NEvent.TLoopDetected' and event.get('LoopNodes'):
            nodes = []
            for item in event['LoopNodes']:
                nodes.append(item['Name'])

            message = 'Loop detected: ' + ' --> '.join(reversed(nodes))
            for item in event['LoopNodes']:
                if item['Type'] == 'Directory':
                    self.errors[item['Name']].add(ce.ConfigureError(message, 0, 0))

        event_sorted = json.dumps(event, sort_keys=True)
        logger.debug('Configure message %s', event_sorted)


class AppendSubscriber(event_handling.SubscriberExcludedTopics):
    topics = set()

    def __init__(self, store):
        self._store = store

    def _action(self, event):
        self._store.append(event)


def _checkout(opts, display=None):
    if not getattr(opts, "checkout", False):
        return

    from yalibrary import vcs

    if vcs.detect_vcs_type(opts.arc_root) != "svn":
        logging.warning("--checkout supported only for SVN, option skipped")
        return

    from devtools.ya.yalibrary import checkout

    fetcher = checkout.VcsFetcher(opts.arc_root)

    from devtools.ya.build import evlog

    fetcher.fetch_base_dirs(
        thin_checkout=opts.thin_checkout, extra_paths=opts.checkout_extra_paths, quiet=opts.checkout_quiet
    )
    fetcher.fetch_dirs(opts.rel_targets, quiet=opts.checkout_quiet)

    missing_dirs = set()
    root_dirs = set()

    opts2 = copy.deepcopy(opts)

    opts2.continue_on_fail = True
    opts2.debug_options += ['x']

    while True:
        events = []
        try:
            with event_handling.EventQueue().subscription_scope(AppendSubscriber(events)) as event_queue:
                lg.build_graph_and_tests(opts2, check=False, event_queue=event_queue, display=display)
        except lg.GraphMalformedException:
            pass
        dirs, root_dirs = evlog.missing_dirs(events, root_dirs)

        if not opts.thin_checkout:
            # in the name of windows
            dirs = normalize_by_dir(dirs, 'contrib/java')

        dirs = [x if not x.startswith(ARC_PREFIX) else x[len(ARC_PREFIX) :] for x in dirs]
        add_arcadia_dirs, add_data_dirs = [], []
        for x in dirs:
            if x not in missing_dirs:
                if x.startswith(ATD_PREFIX):
                    add_data_dirs.append(x)
                else:
                    add_arcadia_dirs.append(x)
        missing_dirs.update(add_arcadia_dirs)
        missing_dirs.update(add_data_dirs)
        if add_arcadia_dirs:
            fetcher.fetch_dirs(add_arcadia_dirs, quiet=opts.checkout_quiet)
        if add_data_dirs:
            fetcher.fetch_dirs(
                [x[len(ATD_PREFIX) :] for x in add_data_dirs],
                quiet=opts.checkout_quiet,
                destination=opts.arcadia_tests_data_path,
                rel_root_path='../arcadia_tests_data',
            )
        if add_arcadia_dirs or add_data_dirs:
            continue
        break


def _build_graph_and_tests(opts, app_ctx, ymake_stats):
    # type: (tp.Any, tp.Any, YmakeTimeStatistic) -> tuple(tp.Any, tp.Any, tp.Any, tp.Any, tp.Any)
    display = getattr(app_ctx, 'display', None)
    _checkout(opts, display)

    printed = set()
    errors_collector = PrintMessageSubscriber()

    configure_time_subscribers = [
        DisplayMessageSubscriber(opts, display, printed),
        ymake_stats,
        errors_collector,
    ]

    if getattr(app_ctx, 'evlog', None):
        configure_time_subscribers.append(
            YmakeEvlogSubscriber(app_ctx.evlog.get_writer('ymake')),
        )

    with app_ctx.event_queue.subscription_scope(*configure_time_subscribers):
        graph, tests, stripped_tests, _, make_files = lg.build_graph_and_tests(opts, check=True, display=display)

    def fix_dir(s):
        if s.startswith('$S/'):
            s = s.replace('$S/', '').replace('/ya.make', '')
        elif s.startswith('$B/'):
            s = os.path.dirname(s).replace('$B/', '')
        return s

    errors = {fix_dir(k): sorted(list(v)) for k, v in errors_collector.errors.items()}
    return graph, tests, stripped_tests, errors, make_files


def _advanced_lock_available(opts):
    return not exts.windows.on_win() and getattr(opts, 'new_store', False)


def make_lock(opts, garbage_dir, write_lock=False, non_blocking=False):
    exts.fs.ensure_dir(garbage_dir)
    lock_file = os.path.join(garbage_dir, '.lock')

    if _advanced_lock_available(opts):
        from exts.plocker import Lock, LOCK_EX, LOCK_NB, LOCK_SH

        timeout = 2 if non_blocking else 1000000000
        return Lock(lock_file, mode='w', timeout=timeout, flags=LOCK_EX | LOCK_NB if write_lock else LOCK_SH | LOCK_NB)

    return exts.filelock.FileLock(lock_file)


def _setup_content_uids(opts, enable):
    if getattr(opts, 'force_content_uids', False):
        logger.debug('content UIDs forced')
        return

    if not getattr(opts, 'request_content_uids', False):
        logger.debug('content UIDs disabled by request')
        return

    if not enable:
        logger.debug('content UIDs disabled: incompatible with cache version')
        return

    logger.debug('content UIDs enabled by request')
    opts.force_content_uids = True


class CacheFactory:
    def __init__(self, opts):
        self._opts = opts

    def get_dist_cache_instance(self):
        cache_instance = self._try_init_bazel_remote_cache()
        if cache_instance is None:
            cache_instance = self._try_init_yt_dist_cache()
        return cache_instance

    def get_local_cache_instance(self, garbage_dir):
        if exts.windows.on_win():
            _setup_content_uids(self._opts, False)
            return uid_store.UidStore(os.path.join(garbage_dir, 'cache', CACHE_GENERATION))

        if getattr(self._opts, 'build_cache', False):
            from yalibrary.toolscache import ACCache, buildcache_enabled

            # garbage_dir is configured in yalibrary.toolscache
            if buildcache_enabled(self._opts):
                _setup_content_uids(self._opts, True)
                return ACCache(os.path.join(garbage_dir, 'cache', '7'))

        _setup_content_uids(self._opts, False)
        if getattr(self._opts, 'new_store', False) and getattr(self._opts, 'new_runner', False):
            from yalibrary.store import new_store

            # FIXME: This suspected to have some race condition in current content_uids implementation (see YA-701)
            store = new_store.NewStore(os.path.join(garbage_dir, 'cache', '6'))
            return store

        return ring_store.RingStore(os.path.join(garbage_dir, 'cache', CACHE_GENERATION))

    def _try_init_bazel_remote_cache(self):
        if not self._can_use_bazel_remote_cache():
            return None
        return self._init_bazel_remote_cache()

    def _try_init_yt_dist_cache(self):
        if not self._can_use_yt_dist_cache():
            return None

        try:
            return self._init_yt_dist_cache()
        except ImportError as e:
            logger.warning("YT store is not available: %s", e)
            return None

    def _init_bazel_remote_cache(self):
        from yalibrary.store.bazel_store import bazel_store

        password = self._get_bazel_password()
        fits_filter = self._get_fits_filter()
        connection_pool_size = self._get_connection_pool_size()

        return bazel_store.BazelStore(
            base_uri=getattr(self._opts, 'bazel_remote_baseuri'),
            username=getattr(self._opts, 'bazel_remote_username', None),
            password=password,
            readonly=getattr(self._opts, 'bazel_remote_readonly', True),
            max_file_size=getattr(self._opts, 'dist_cache_max_file_size', 0),
            max_connections=connection_pool_size,
            fits_filter=fits_filter,
        )

    def _init_yt_dist_cache(self):
        from yalibrary.store.yt_store import yt_store

        token = self._get_yt_token()
        yt_store_class = yt_store.YndexerYtStore if self._opts.yt_replace_result_yt_upload_only else yt_store.YtStore

        return yt_store_class(
            self._opts.yt_proxy,
            self._opts.yt_dir,
            self._opts.yt_cache_filter,
            token=token,
            readonly=self._opts.yt_readonly,
            create_tables=self._opts.yt_create_tables,
            max_file_size=getattr(self._opts, 'dist_cache_max_file_size', 0),
            max_cache_size=self._opts.yt_max_cache_size,
            ttl=self._opts.yt_store_ttl,
            heater_mode=not self._opts.yt_store_wt,
            stager=stager,
            with_self_uid=self._opts.yt_self_uid,
            new_client=self._opts.yt_store_cpp_client,
            probe_before_put=self._opts.yt_store_probe_before_put,
            probe_before_put_min_size=self._opts.yt_store_probe_before_put_min_size,
        )

    def _can_use_bazel_remote_cache(self):
        return all(
            (
                getattr(self._opts, 'bazel_remote_store', False),
                getattr(self._opts, 'bazel_remote_baseuri', None),
                not (getattr(self._opts, 'use_distbuild', False) and getattr(self._opts, 'bazel_readonly', False)),
            )
        )

    def _can_use_yt_dist_cache(self):
        return all(
            (
                getattr(self._opts, 'build_threads') > 0,
                getattr(self._opts, 'yt_store', False),
                not (getattr(self._opts, 'use_distbuild', False) and getattr(self._opts, 'yt_readonly', False)),
            )
        )

    def _get_bazel_password(self):
        password = getattr(self._opts, 'bazel_remote_password', None)
        password_file = getattr(self._opts, 'bazel_remote_password_file', None)

        if not password and password_file:
            logger.debug("Using '%s' file to obtain bazel remote password", password_file)
            try:
                with open(password_file) as afile:
                    password = afile.read()
            except Exception as e:
                logger.warning("Failed to read bazel remote password file: %s", e)
        return password

    def _get_yt_token(self):
        token = self._opts.yt_token or self._opts.oauth_token
        if not token:
            try:
                from yalibrary import oauth

                token = oauth.get_token(core_config.get_user())
            except Exception as e:
                logger.warning("Failed to get YT token: %s", e)
        return token

    def _get_fits_filter(self):
        def fits_filter(node):
            if not isinstance(node, dict):
                node = node.args
            return is_dist_cache_suitable(node, None, self._opts)

        return fits_filter

    def _get_connection_pool_size(self):
        return getattr(self._opts, 'dist_store_threads', 24) + getattr(self._opts, 'build_threads', 0)


def make_dist_cache(dist_cache_future, opts, graph_nodes, heater_mode):
    if not graph_nodes:
        return None

    self_uids = [node.get('self_uid') or node['uid'] for node in graph_nodes]
    uids = [node['uid'] for node in graph_nodes]

    try:
        logger.debug("Waiting for dist cache setup")
        cache = dist_cache_future()
        if cache:
            logger.debug("Loading meta from dist cache")

            # needed for catching an error in this rare scenario
            _async = not (opts.yt_store_exclusive or heater_mode)

            cache.prepare(
                self_uids,
                uids,
                refresh_on_read=opts.yt_store_refresh_on_read,
                content_uids=opts.force_content_uids,
                _async=_async,
            )

        logger.debug("Dist cache prepared")
        return cache
    except Exception as e:
        err = str(e)
        devtools.ya.core.report.telemetry.report(
            devtools.ya.core.report.ReportTypes.YT_CACHE_ERROR,
            {
                "error": "Can't use YT cache",
                "user": core_config.get_user(),
            },
        )
        logger.warning(
            'Can\'t use dist cache: %s... <Truncated. Complete message will be available in debug logs>', err[:100]
        )
        logger.debug('Can\'t use dist cache: %s', err)
        if opts.yt_store_exclusive or heater_mode:
            raise
        return None


def make_runner():
    from yalibrary.runner import runner3

    return runner3.run


def load_configure_errors(errors):
    loaded_errors = collections.defaultdict(list)

    if not errors:
        return loaded_errors

    for path, error_list in errors.items():
        if error_list == 'OK':
            loaded_errors[path].append(ce.ConfigureError('OK', 0, 0))
            continue

        for error in error_list:
            if isinstance(error, list) and len(error) == 3:
                loaded = ce.ConfigureError(*error)
            else:
                logger.debug('Suspicious configure error type: %s (error: %s)', type(error), error)
                loaded = ce.ConfigureError(str(error), 0, 0)
            loaded_errors[path].append(loaded)

    return loaded_errors


def configure_build_graph_cache_dir(app_ctx, opts):
    if opts.build_graph_cache_heater:
        return

    if build_graph_cache:
        build_graph_cache_resource_dir = build_graph_cache.BuildGraphCacheResourceDir(app_ctx, opts)

    try:
        logger.debug("Build graph cache processing started")
        if not build_graph_cache or not build_graph_cache_resource_dir.enabled():
            logger.debug("Build graph cache processing disabled")
            if not build_graph_cache:
                logger.debug('Build graph cache is not available in opensource')
                return

            if build_graph_cache.is_cache_provided(opts) and not opts.build_graph_cache_cl and not opts.distbuild_patch:
                logger.warning(
                    '--build-graph-cache-dir/--build-graph-cache-archive needs change list provided with --build-graph-cache-cl for improved ymake performance'
                )
                return

            opts.build_graph_cache_cl = (
                build_graph_cache.prepare_change_list(opts) if build_graph_cache.is_cache_provided(opts) else None
            )
            return

        # Validate opts.build_graph_cache_resource.
        logger.debug("Getting build graph cache resource id")
        opts.build_graph_cache_resource = build_graph_cache_resource_dir.resource_id

        if build_graph_cache_resource_dir.safe_ymake_cache:
            logger.debug(
                'Safe caches set for resource {}: {}'.format(
                    build_graph_cache_resource_dir.resource_id, build_graph_cache_resource_dir.safe_ymake_cache
                )
            )
            if opts.build_graph_use_ymake_cache_params and 'normal' in opts.build_graph_use_ymake_cache_params:
                logger.debug(
                    'build_graph_use_ymake_cache_params before downgrade to safe caches: {}'.format(
                        opts.build_graph_use_ymake_cache_params
                    )
                )
                opts.build_graph_use_ymake_cache_params['normal'] = CacheKind.get_ymake_option(
                    build_graph_cache_resource_dir.safe_ymake_cache
                )
                opts.build_graph_use_ymake_cache_params_str = json.dumps(opts.build_graph_use_ymake_cache_params)
                logger.debug(
                    'build_graph_use_ymake_cache_params after downgrade to safe caches: {}'.format(
                        opts.build_graph_use_ymake_cache_params
                    )
                )

        download = not (opts.make_context_on_distbuild or opts.make_context_on_distbuild_only or opts.make_context_only)
        if download:
            logger.debug("Downloading build graph cache resource")
            opts.build_graph_cache_archive = build_graph_cache_resource_dir.download_build_graph_cache()

        logger.debug("Getting change list for build graph cache")
        opts.build_graph_cache_cl = build_graph_cache_resource_dir.merge_change_lists(opts)
        logger.debug("Build graph cache enabled")
    except Exception as e:
        build_graph_cache.reset_build_graph_cache(opts)
        logger.exception("(ya_make) Build graph cache disabled %s", e)


def get_suites_exit_code(suites, test_fail_exit_code=const.TestRunExitCode.Failed):
    statuses = {suite.get_status() for suite in suites if not suite.is_skipped()}
    if not statuses or statuses == {const.Status.GOOD}:
        exit_code = 0
    elif const.Status.INTERNAL in statuses:
        exit_code = const.TestRunExitCode.InfrastructureError
    else:
        exit_code = int(test_fail_exit_code)
    return exit_code


# TODO: Merge to Context
class BuildContext:
    @classmethod
    def load(cls, params, app_ctx, data):
        kwargs = {'encoding': 'utf-8'}
        builder = YaMake(
            params,
            app_ctx,
            graph=data.get('graph'),
            tests=[
                cPickle.loads(six.ensure_binary(pickled_test, encoding='latin-1'), **kwargs)
                for pickled_test in data['tests'].values()
            ],
            stripped_tests=[
                cPickle.loads(six.ensure_binary(pickled_test, encoding='latin-1'), **kwargs)
                for pickled_test in data.get('stripped_tests', {}).values()
            ],
            configure_errors=data['configure_errors'],
            make_files=data['make_files'],
        )
        return BuildContext(builder, owners=data['owners'])

    def __init__(self, builder, owners=None, ctx=None):
        self.builder = builder
        self.ctx = ctx
        self.owners = owners or builder.get_owners()

    def save(self):
        ctx = self.ctx or self.builder.ctx
        return {
            'configure_errors': ctx.configure_errors,
            'tests': {test.uid: six.ensure_str(cPickle.dumps(test), encoding='latin-1') for test in ctx.tests},
            'stripped_tests': {
                test.uid: six.ensure_str(cPickle.dumps(test), encoding='latin-1') for test in ctx.stripped_tests
            },
            'make_files': ctx.make_files,
            'owners': self.owners,
            'graph': ctx.full_graph,
        }


def is_local_build_with_tests(opts):
    return (
        not (os.environ.get('AUTOCHECK', False) or hasattr(opts, 'flags') and opts.flags.get('AUTOCHECK', False))
        and opts.run_tests
    )


def need_cache_test_statuses(opts):
    if opts.cache_test_statuses is not None:
        return opts.cache_test_statuses
    return is_local_build_with_tests(opts)


# XXX see YA-1354
def replace_dist_cache_results(graph, opts, dist_cache, app_ctx):
    if not dist_cache:
        return []

    orig_result = set(graph['result'])

    def suitable(node):
        return is_dist_cache_suitable(node, orig_result, opts)

    def check(node):
        app_ctx.state.check_cancel_state()
        uid = node['uid']
        if dist_cache.fits(node) and suitable(node) and not dist_cache.has(uid):
            return uid
        return None

    result = set(core_async.par_map(check, graph['graph'], opts.dist_store_threads))
    result.discard(None)
    return list(sorted(result))


def get_module_type(node):
    return node.get('target_properties', {}).get('module_type')


def is_target_binary(node):
    is_binary = get_module_type(node) == 'bin'
    return is_binary and not node.get('host_platform')


def is_bundle(node):
    return get_module_type(node) == 'bundle'


def is_dist_cache_suitable(node, result, opts):
    if opts.dist_cache_evict_binaries and is_target_binary(node):
        return False

    if opts.dist_cache_evict_bundles and is_bundle(node):
        return False

    module_tag = node.get('target_properties', {}).get('module_tag', None)
    if module_tag in ('jar_runable', 'jar_runnable', 'jar_testable'):
        return False

    if get_module_type(node):
        return True

    # If all checks are passed - all result nodes are suitable
    if result is not None:
        return node['uid'] in result
    # If result node are not specified -
    return True


# XXX see YA-1354
def replace_yt_results(graph, opts, dist_cache):
    assert 'result' in graph

    if not dist_cache:
        return [], []

    new_results = []
    cached_results = []

    if not opts.yt_cache_filter:
        original_results = set(graph.get('result', []))
        network_limited_nodes = set()

        def network_limited(node):
            return node.get("requirements", {}).get("network") == "full"

        def suitable(node):
            if opts.yt_replace_result_yt_upload_only:
                return any(out.endswith('.ydx.pb2.yt') for out in node.get('outputs', []))
            if opts.yt_replace_result_add_objects and any(
                out.endswith('.o') or out.endswith('.obj') for out in node.get('outputs', [])
            ):
                return True

            return is_dist_cache_suitable(node, original_results, opts)

        first_pass = []
        for node in graph['graph']:
            uid = node.get('uid')

            if network_limited(node):
                network_limited_nodes.add(uid)

            if dist_cache.fits(node) and suitable(node):
                # (node, new_result)
                first_pass.append((node, not dist_cache.has(node.get('uid'))))

        # Another pass to filter out network intensive nodes.
        for node, new_result in first_pass:
            uid = node.get('uid')
            if not opts.yt_replace_result_yt_upload_only:
                if uid in network_limited_nodes:
                    continue
                deps = node.get('deps', [])
                # It is dependence on single fetch_from-like node.
                if len(deps) == 1 and deps[0] in network_limited_nodes:
                    continue

            if new_result:
                new_results.append(uid)
            else:
                cached_results.append(uid)
    else:
        for node in graph.get('graph'):
            uid = node.get('uid')
            if dist_cache.fits(node) and not dist_cache.has(uid):
                new_results.append(uid)

    return new_results, cached_results


class Context:
    RELEASED = {}  # sentinel to mark released full graph

    def __init__(
        self,
        opts,
        app_ctx,
        graph=None,
        tests=None,
        stripped_tests=None,
        configure_errors=None,
        make_files=None,
    ):
        self._timer = timer = exts.timer.Timer('context_creation')
        context_creation_stage = stager.start('context_creation')

        self.stage_times = {}

        self._cache_factory = CacheFactory(opts)
        self._full_graph = None

        self.opts = opts
        self.cache_test_statuses = need_cache_test_statuses(opts)

        def notify_locked():
            logger.info('Waiting for other build process to finish...')

        self.clear_cache_tray(opts)

        self.local_runner_ready = core_async.ProperEvent()

        self._lock = make_lock(opts, self.garbage_dir)

        self.clear_garbage(opts)

        other_build_notifier = core_async.CancellableTimer(notify_locked, 1.0)
        other_build_notifier.start(cancel_on=lambda: self.lock())

        self.threads = self.opts.build_threads

        self.create_output = self.opts.output_root is not None
        self.create_symlinks = getattr(self.opts, 'create_symlinks', True) and not exts.windows.on_win()

        def get_suppression_conf():
            suppress_outputs = getattr(self.opts, 'suppress_outputs', [])
            default_suppress_outputs = getattr(self.opts, 'default_suppress_outputs', [])
            add_result = getattr(self.opts, 'add_result', [])
            # '.a' in suppress_outputs overrides '.specific.a' in add_result
            add_result = [i for i in add_result if not any([i.endswith(x) for x in suppress_outputs])]
            # '.a' in add_result overrides '.specific.a' in default_suppress_outputs
            default_suppress_outputs = [
                i for i in default_suppress_outputs if not any([i.endswith(x) for x in add_result])
            ]

            return {
                'add_result': add_result,
                'suppress_outputs': suppress_outputs,
                'default_suppress_outputs': default_suppress_outputs,
            }

        self.suppress_outputs_conf = get_suppression_conf()

        self.output_replacements = [(opts.oauth_token, "<YA-TOKEN>")] if opts.oauth_token else []

        dist_cache_future = core_async.future(lambda: self._cache_factory.get_dist_cache_instance())

        display = getattr(app_ctx, 'display', None)
        print_status = get_print_status_func(opts, display, logger)

        self.ymake_stats = YmakeTimeStatistic()
        self.configure_errors = {}
        if graph is not None and tests is not None:
            self.graph = graph
            self.tests = tests
            self.stripped_tests = []
            if stripped_tests is not None:
                self.stripped_tests = stripped_tests
            self.configure_errors = load_configure_errors(configure_errors)
            self.make_files = make_files or []
        elif opts.custom_json is not None and opts.custom_json:
            with udopen(opts.custom_json, "rb") as custom_json_file:
                self.graph = sjson.load(custom_json_file, intern_keys=True, intern_vals=True)
                lg.finalize_graph(self.graph, opts)
            self.tests = []
            self.stripped_tests = []
            self.make_files = []
        else:
            (
                self.graph,
                self.tests,
                self.stripped_tests,
                self.configure_errors,
                self.make_files,
            ) = _build_graph_and_tests(self.opts, app_ctx, self.ymake_stats)
            timer.show_step("graph_and_tests finished")

            if self.configure_errors and not opts.continue_on_fail:
                raise ConfigurationError()

        self._populate_graph_fields()

        print_status("Configuring local and dist store caches")

        with stager.scope('configure-dist-store-cache'):
            self.dist_cache = make_dist_cache(
                dist_cache_future, self.opts, self.graph['graph'], heater_mode=not self.opts.yt_store_wt
            )
        with stager.scope('configure-local-cache'):
            self.cache = self._cache_factory.get_local_cache_instance(self.garbage_dir)

        print_status("Configuration done. Preparing for execution")

        sandbox_run_test_uids = set(self.get_context().get('sandbox_run_test_result_uids', []))
        logger.debug("sandbox_run_test_uids: %s", sandbox_run_test_uids)

        # XXX see YA-1354
        if opts.bazel_remote_store and (not opts.bazel_remote_readonly or opts.dist_cache_evict_cached):
            self.graph['result'] = replace_dist_cache_results(self.graph, opts, self.dist_cache, app_ctx)
            logger.debug("Strip graph due bazel_remote_store mode")
            self.graph = lg.strip_graph(self.graph)
            results = set(self.graph['result'])
            self.tests = [x for x in self.tests if x.uid in results]
        # XXX see YA-1354
        elif (opts.yt_replace_result or opts.dist_cache_evict_cached) and not opts.add_result:
            new_results, cached_results = replace_yt_results(self.graph, opts, self.dist_cache)
            self.graph['result'] = new_results
            logger.debug("Strip graph due yt_replace_result mode")
            self.graph = lg.strip_graph(self.graph)
            self.yt_cached_results = cached_results
            self.yt_not_cached_results = new_results
            results = set(self.graph['result'])
            self.tests = [x for x in self.tests if x.uid in results]

        elif opts.frepkage_target_uid:
            assert opts.frepkage_target_uid in graph['result'], (opts.frepkage_target_uid, graph['result'])
            logger.debug("Strip graph using %s uid as single result", opts.frepkage_target_uid)
            self.graph = lg.strip_graph(self.graph, result=[opts.frepkage_target_uid])
            self.tests = [x for x in self.tests if x.uid == opts.frepkage_target_uid]
            assert self.tests

        elif sandbox_run_test_uids:
            # XXX This is a place for further enhancement.
            # Prerequisites:
            #  - support all vcs
            #  - support branches
            #  - we can't get the graph without patch (to minimize uploading changes)
            #  - should take into account vcs untracked changes which might affect on test
            #  - local repository state might be heterogeneous (intentionally or not) - some parts of the repo might have different revisions, etc
            #  - can't apply patch over arcadia in the Sandbox task without information what inputs were removed or renamed
            #  - arbitrary build configurations specified by user
            # Current implementation pros:
            #  - all prerequisites are taken in to account
            #  - frepkage is fully hermetic and sandbox task doesn't need to setup arcadia repo - frepakge contains all required inputs
            #  - yt cache and local cache are reused
            #  - works in development mode with external inputs in graph (trunk ya-bin, --ymake-bin PATH, --test-tool-bin PATH)
            # Cons:
            #  - repkage contains all inputs from the graph, what can be excessive if there a lot of tests without ya:force_sandbox tag in the graph
            #    Their input data will also be uploaded, but will not be used.
            #    # TODO
            #      ymake/ya-bin should set proper inputs to the nodes which could be stripped.
            #      right now some nodes have not fully qualified inputs - this problem is masked and overlapped by the graph's 'inputs' section
            #  - doesn't support semidist mode (build arts on distbuild, run tests locally)
            #  - extremely huge input is a bottleneck
            # Notes:
            #  - currently doesn't support ATD (it's quite easy to fix, but we want people to use Sandbox instead of ATD)

            if self.opts.use_distbuild:
                raise devtools.ya.core.yarg.FlagNotSupportedException(
                    "--run-tagged-tests-on-sandbox option doesn't support --dist mode"
                )

            if self.threads or self.opts.force_create_frepkage:
                print_status('Preparing frozen repository package')

                def get_sandbox_graph(graph):
                    graph = lg.strip_graph(graph, result=sandbox_run_test_uids)
                    # strip_graph() returns graph's shallow copy - deepcopy 'conf' section to save original one
                    graph['conf'] = copy.deepcopy(graph['conf'])
                    del graph['conf']['context']['sandbox_run_test_result_uids']
                    return graph

                frepkage_file = frepkage.create_frepkage(
                    build_context=BuildContext(builder=None, owners={'dummy': None}, ctx=self).save(),
                    graph=get_sandbox_graph(self.graph),
                    arc_root=self.opts.arc_root,
                )

                if self.opts.force_create_frepkage:
                    if os.path.exists(self.opts.force_create_frepkage):
                        os.unlink(self.opts.force_create_frepkage)
                    # Copy only valid and fully generated frepkage
                    exts.fs.hardlink_or_copy(frepkage_file, self.opts.force_create_frepkage)
            else:
                # Don't waste time preparing frepkage if -j0 is requested
                frepkage_file = 'frepkage_was_not_generated_due_-j0'

            from devtools.ya.test import test_node

            # All tests have same global resources
            test_global_resources = self.tests[0].global_resources

            # Move frepkage uploading to the separate node in the graph to avoid a bottleneck
            upload_node, upload_res_info_filename = test_node.create_upload_frepkage_node(
                frepkage_file, test_global_resources, self.opts
            )
            self.graph['graph'].append(upload_node)
            # Create node to populate token to the vault which will be used in the task to access YT build cache
            # to speed up Sandbox task created by sandbox_run_test node
            populate_node = test_node.create_populate_token_to_sandbox_vault_node(test_global_resources, self.opts)
            self.graph['graph'].append(populate_node)

            # Replace ya:force_sandbox tagged result test nodes with sandbox_run_test node
            test_map = {x.uid: x for x in self.tests}
            nodes_map = {node['uid']: node for node in self.graph['graph']}
            for uid in sandbox_run_test_uids:
                node = nodes_map[uid]
                new_node = test_node.create_sandbox_run_test_node(
                    node,
                    test_map[uid],
                    nodes_map,
                    frepkage_res_info=upload_res_info_filename,
                    deps=[upload_node['uid'], populate_node['uid']],
                    opts=self.opts,
                )
                node.clear()
                node.update(new_node)

            # Strip dangling test nodes (such can be appeared if FORK_*TEST or --tests-retries were specified)
            self.graph = lg.strip_graph(self.graph)

            timer.show_step("sandbox_run_test_processing finished")

        # We assume that graph won't be modified after this point. Lite graph should be same as full one -- but lite!

        if self.opts.use_distbuild:
            self._full_graph = self.graph
            self.graph = lg.build_lite_graph(self.graph)

        if app_config.in_house:
            import yalibrary.diagnostics as diag

            if diag.is_active():
                diag.save('ya-make-full-graph', graph=json.dumps(self.graph, sort_keys=True, indent=4, default=str))
                timer.show_step("full graph is dumped")

        if opts.show_command:
            self.threads = 0

            for flt in self.opts.show_command:
                for node, full_match in lg.filter_nodes_by_output(self.graph, flt, warn=True):
                    print(json.dumps(node, sort_keys=True, indent=4, separators=(',', ': ')))
            timer.show_step("show_command finished")

        self._dump_graph_if_needed()

        self.runner = make_runner()

        if opts.cache_stat:
            self.cache.analyze(app_ctx.display)
            timer.show_step("cache_stat finished")

        self.output_result = result_store.ResultStore(self.output_dir) if self.create_output else None
        self.symlink_result = (
            result_store.SymlinkResultStore(self.symres_dir, getattr(self.opts, 'symlink_root', None) or self.src_dir)
            if self.create_symlinks
            else None
        )

        # XXX: Legacy (DEVTOOLS-1128)
        self.install_result = result_store.LegacyInstallResultStore(opts.install_dir) if opts.install_dir else None
        self.bin_result = (
            result_store.LegacyInstallResultStore(opts.generate_bin_dir) if opts.generate_bin_dir else None
        )
        self.lib_result = (
            result_store.LegacyInstallResultStore(opts.generate_lib_dir) if opts.generate_lib_dir else None
        )

        # XXX: Legacy (DEVTOOLS-1128) + build_root_set cleanup
        if (
            opts.run_tests <= 0
            and self.threads > 0
            and not opts.keep_temps
            and not self.output_result
            and not self.symlink_result
            and not self.install_result
            and not self.bin_result
            and not self.lib_result
        ):
            logger.warning(
                'Persistent storage for results is not specified. '
                + 'Remove --no-src-links option (*nix systems) and/or use -o/--output option, see details in help.'
            )

        context_creation_stage.finish()
        timer.show_step("context_creation finished")
        self.stage_times['context_creation'] = timer.full_duration()

    def get_context(self):
        return self.graph.get('conf', {}).get('context', {})

    def lock(self):
        self._lock.acquire()

    def unlock(self):
        self._lock.release()

    def clear_cache_tray(self, opts):
        if _advanced_lock_available(opts):
            from exts.plocker import LockException

            try:
                with make_lock(opts, self.garbage_dir, write_lock=True, non_blocking=True):
                    self.cache = self._cache_factory.get_local_cache_instance(self.garbage_dir)
                    if hasattr(self.cache, 'clear_tray'):
                        self.cache.clear_tray()
            except LockException:
                pass

    def clear_garbage(self, opts):
        if not self.opts.do_clear:
            return
        # Do not remove lock file while locked
        assert self._lock
        with make_lock(opts, self.garbage_dir, write_lock=True):
            for filename in os.listdir(self.garbage_dir):
                if filename == self.lock_name:
                    continue
                exts.fs.remove_tree_safe(os.path.join(self.garbage_dir, filename))

    @property
    def full_graph(self):
        assert self._full_graph is not Context.RELEASED, "full_graph is requested after release"
        if self._full_graph is None:
            return self.graph
        else:
            return self._full_graph

    def release_full_graph(self):
        assert (
            self._full_graph is not Context.RELEASED and self._full_graph is not None
        ), "releasing an uninitialized or already released graph"
        result = self._full_graph
        self._full_graph = Context.RELEASED
        return result

    @property
    def abs_targets(self):
        return self.opts.abs_targets or [os.getcwd()]

    @property
    def garbage_dir(self):
        return self.opts.bld_dir

    @property
    def conf_dir(self):
        return os.path.join(self.garbage_dir, 'conf')

    @property
    def src_dir(self):
        return self.opts.arc_root

    @property
    def output_dir(self):
        return self.opts.output_root

    @property
    def symres_dir(self):
        return os.path.join(self.garbage_dir, 'symres')

    @property
    def res_dir(self):
        return core_config.tool_root(toolscache_version(self.opts))

    @property
    def lock_name(self):
        return '.lock'

    @property
    def lock_file(self):
        return os.path.join(self.garbage_dir, self.lock_name)

    def _populate_graph_fields(self):
        if self.graph is None:
            return

        graph_conf = self.graph['conf']
        graph_conf['keepon'] = self.opts.continue_on_fail
        graph_conf.update(gp.gen_description())
        if self.opts.default_node_requirements:
            graph_conf['default_node_requirements'] = self.opts.default_node_requirements
        if self.opts.use_distbuild:
            if self.opts.distbuild_pool:
                graph_conf['pool'] = self.opts.distbuild_pool
            if self.opts.dist_priority:
                graph_conf['priority'] = self.opts.dist_priority
            if self.opts.distbuild_cluster:
                graph_conf['cluster'] = self.opts.distbuild_cluster
            if self.opts.distbuild_cluster:
                graph_conf['cluster'] = self.opts.distbuild_cluster
            elif self.opts.coordinators_filter:
                graph_conf['coordinator'] = self.opts.coordinators_filter
            if self.opts.cache_namespace:
                graph_conf['namespace'] = self.opts.cache_namespace

            if self.opts.trace_context_json:
                # Trace context should be extracted earlier but since `ya` doesn't use OTEL tracing
                # we extract it here and pass it directly to DistBuild.
                try:
                    carrier = json.loads(self.opts.trace_context_json)
                    # The below logic should be implemented with OpenTelemetry Python API
                    # but to avoid extra dependencies we do simplified parsing ourselves
                    traceparent = carrier['traceparent']
                    __, trace_id, span_id, trace_flags = traceparent.split('-')
                    # Check if this trace branch is sampled
                    if int(trace_flags):
                        # Ideally we should just pass trace context's fields/headers ('traceparent', 'tracestate')
                        # through but we need to agree this protocol change with the DistBuild team first
                        self.graph['conf']['trace_context'] = {
                            'project': 'distbuild',
                            'trace_id': trace_id,
                            'span_id': span_id,
                        }
                except Exception as exc:
                    logger.warning(
                        'Failed to load the provided trace context and propagate it to DistBuild.',
                        exc_info=exc,
                    )

    def _dump_graph_if_needed(self):
        if not self.opts.dump_graph:
            return

        if self.opts.dump_graph_file:
            with open(self.opts.dump_graph_file, 'w') as gf:
                json.dump(self.full_graph, gf, sort_keys=True, indent=4, default=str)
        else:
            stdout = self.opts.stdout or sys.stdout
            json.dump(self.full_graph, stdout, sort_keys=True, indent=4, default=str)
            stdout.flush()

        self._timer.show_step("dump_graph finished")


def extension(f):
    p = f.rfind('.')

    if p > 0:
        return f[p + 1 :]


def get_deps(targets, graph, build_result, dest_dir):
    if len(targets) > 1:
        logger.warning('Don\'t know which deps to dump. Candidates are %s', ', '.join(targets))
        return

    import devtools.ya.jbuild.gen.base as base
    import devtools.ya.jbuild.gen.actions.funcs as funcs

    result = frozenset(graph['result'])
    target = targets[0]

    for node in graph['graph']:
        if 'get-deps' in node.get('java_tags', []) and node['uid'] in result:
            assert node['uid'] in build_result.ok_nodes

            for out, res in zip(node['outputs'], build_result.ok_nodes[node['uid']]):
                this_target = base.relativize(os.path.dirname(os.path.dirname(out)))

                if target is None:
                    target = this_target

                elif this_target != target:
                    continue

                if 'artifact' in res:
                    jar_with_deps = res['artifact']

                elif 'symlink' in res:
                    jar_with_deps = res['symlink']

                else:
                    raise Exception('Build result for {} is invalid: {}'.format(out, str(res)))

                unpack_to = os.path.join(dest_dir, os.path.basename(os.path.dirname(jar_with_deps)))

                exts.fs.ensure_removed(unpack_to)
                exts.fs.create_dirs(unpack_to)
                funcs.jarx(jar_with_deps, unpack_to)()

    if target is not None:
        logger.info('Successfully dumped deps of %s: %s', target, dest_dir)


class YaMake:
    def __init__(
        self,
        opts,
        app_ctx,
        graph=None,
        tests=None,
        stripped_tests=None,
        configure_errors=None,
        make_files=None,
    ):
        self.opts = opts
        if getattr(opts, 'pgo_user_path', None):
            setattr(opts, 'pgo_path', merge_pgo_profiles(opts.pgo_user_path))
        self.app_ctx = app_ctx
        self._owners = None
        self._make_files = None
        self.raw_build_result = None
        self.build_root = None
        self.misc_build_info_dir = None
        self.arc_root = None
        self._setup(opts)
        self._validate_opts()
        self.ctx = Context(
            self.opts,
            app_ctx=app_ctx,
            graph=graph,
            tests=tests,
            stripped_tests=stripped_tests,
            configure_errors=configure_errors,
            make_files=make_files,
        )
        self._post_clean_setup(opts)
        self.build_result = br.BuildResult({}, {}, {})
        self.exit_code = 0
        self._build_results_listener = None
        self._output_root = None
        self._report = None
        self._reports_generator = None
        self._slot_time_listener = None

    def _setup(self, opts):
        self.opts = opts
        self.arc_root = opts.arc_root
        if not os.path.isabs(self.opts.arcadia_tests_data_path):
            self.opts.arcadia_tests_data_path = os.path.join(
                os.path.dirname(self.arc_root), self.opts.arcadia_tests_data_path
            )
        self._setup_environment()

    def _post_clean_setup(self, opts):
        self.misc_build_info_dir = getattr(opts, 'misc_build_info_dir', None) or tempfile.mkdtemp(
            prefix='ya-misc-build-info-dir'
        )
        exts.fs.create_dirs(self.misc_build_info_dir)

    def _setup_build_root(self, build_root):
        assert build_root, "Expected not empty build_root value"
        self.build_root = os.path.abspath(build_root)
        exts.fs.create_dirs(self.build_root)

    def _generate_report(self):
        logger.debug("Generating results report")
        build_report.generate_results_report(self)
        build_report.generate_empty_tests_result_report(self)

    def _dump_json(self, filename, content):
        with open(os.path.join(self.misc_build_info_dir, filename), 'w') as fp:
            json.dump(content, fp=fp, indent=4, sort_keys=True)

    def get_owners(self):
        if self._owners is None:
            owners = {}
            for entry in self.ctx.make_files:
                if 'OWNERS' in entry and 'PATH' in entry:
                    logins, groups = ow.make_logins_and_groups(entry['OWNERS'].split())
                    path = entry['PATH']
                    if path == '$S' or path.startswith('$S/'):
                        path = path[3:]
                    ow.add_owner(owners, path, logins, groups)
            self._owners = owners
        return self._owners

    @func.lazy_property
    def expected_build_targets(self):
        gen_build_targets_result = build_facade.gen_build_targets(
            build_root=self.opts.custom_build_directory,
            build_type=self.opts.build_type,
            build_targets=self.opts.abs_targets,
            debug_options=self.opts.debug_options,
            flags=self.opts.flags,
            ymake_bin=self.opts.ymake_bin,
        )
        if gen_build_targets_result.exit_code != 0:
            cp.profile_value('gen_build_targets_exit_code', gen_build_targets_result.exit_code)
            raise ConfigurationError(
                'Unable to get build targets names. Ymake says {}'.format(gen_build_targets_result.stderr.strip())
            )

        return gen_build_targets_result.stdout.split()

    def get_make_files(self):
        if self._make_files is None:
            make_files = set()
            for entry in self.ctx.make_files:
                if 'PATH' in entry:
                    make_files.add(entry['PATH'].replace('$S/', ''))
            self._make_files = make_files
        return self._make_files

    def add_owner(self, path, logins, groups):
        ow.add_owner(self.get_owners(), path, logins, groups)

    def _setup_compact_for_gc(self):
        if not self.opts.strip_cache:
            return
        if not hasattr(self.ctx.cache, 'compact'):
            return

        if hasattr(self.ctx.cache, 'set_max_age'):
            self.ctx.cache.set_max_age(time.time())
        else:
            setattr(self.ctx.opts, 'new_store_ttl', 0)
            setattr(self.ctx.opts, 'cache_size', 0)

    def _strip_cache(self, threads):
        if not self.opts.strip_cache:
            return

        if hasattr(self.ctx.cache, 'compact'):
            if threads == 0:
                for node in self.ctx.graph.get('graph', []):
                    self.ctx.cache.has(node['uid'])  # touch uid

                self.ctx.cache.compact(
                    getattr(self.ctx.opts, 'new_store_ttl'), getattr(self.ctx.opts, 'cache_size'), self.app_ctx.state
                )
        else:
            node_uids = {node['uid'] for node in self.ctx.graph.get('graph', [])}
            self.ctx.cache.strip(lambda info: info.uid in node_uids)

        tc_force_gc(getattr(self.ctx.opts, 'cache_size'))

    def _setup_environment(self):
        os.environ['PORT_SYNC_PATH'] = os.path.join(self.opts.bld_dir, 'port_sync_dir')
        if (
            getattr(self.opts, "multiplex_ssh", False)
            and getattr(self.opts, "checkout", False)
            and not exts.windows.on_win()
        ):
            # https://stackoverflow.com/questions/34829600/why-is-the-maximal-path-length-allowed-for-unix-sockets-on-linux-108
            dirname = None
            candidates = [lambda: self.opts.bld_dir, lambda: tempfile.mkdtemp(prefix='yatmp')]
            for get_name in candidates:
                name = get_name()
                if len(name) < 70:
                    dirname = name
                    break
            if dirname:
                control_path = os.path.join(dirname, 'ymux_%p_%r')
                logger.debug(
                    "SSH multiplexing is enabled: control path: %s (%s)",
                    control_path,
                    os.path.exists(os.path.dirname(control_path)),
                )
                multiplex_opts = '-o ControlMaster=auto -o ControlPersist=3s -o ControlPath={}'.format(control_path)

                svn_ssh = os.environ.get('SVN_SSH')
                if not svn_ssh:
                    if not self.arc_root:
                        logger.debug("SSH multiplexing is disabled: no arcadia root specified")
                        return

                    from urllib.parse import urlparse
                    import yalibrary.svn

                    svn_env = os.environ.copy()
                    svn_env['LC_ALL'] = 'C'
                    with exts.tmp.environment(svn_env):
                        try:
                            root_info = yalibrary.svn.svn_info(self.arc_root)
                        except yalibrary.svn.SvnRuntimeError as e:
                            if "is not a working copy" in e.stderr:
                                logger.debug(
                                    "SSH multiplexing is disabled: %s is not an svn working copy", self.arc_root
                                )
                                return
                            raise
                    svn_ssh = urlparse(root_info['url']).scheme.split('+')[-1]

                os.environ['SVN_SSH'] = "{} {}".format(svn_ssh, multiplex_opts)
                logger.debug("SVN_SSH=%s", os.environ['SVN_SSH'])
            else:
                logger.debug(
                    "SSH multiplexing is disabled: because none of the candidates meets the requirements (%s)",
                    candidates,
                )

    def _setup_repo_state(self):
        suites = [t for t in self.ctx.tests if not t.is_skipped()]
        source_root = self.ctx.src_dir
        for s in suites:
            test_project_path = getattr(s, "project_path", None)
            if test_project_path is None:
                continue
            test_results = os.path.join(source_root, test_project_path, "test-results")
            if os.path.islink(test_results):
                logger.debug("test-results link found: %s", test_results)
                os.unlink(test_results)

    def _setup_reports(self):
        self._build_results_listener = pr.CompositeResultsListener([])
        test_results_path = self._get_results_root()
        test_node_listener = pr.TestNodeListener(self.ctx.tests, test_results_path, None)
        if self.opts.dump_failed_node_info_to_evlog:
            self._build_results_listener.add(pr.FailedNodeListener(self.app_ctx.evlog))

        if all(
            [
                self.opts.json_line_report_file is None,
                self.opts.build_results_report_file is None,
                self.opts.streaming_report_id is None
                or (self.opts.streaming_report_url is None and not self.opts.report_to_ci),
            ]
        ):
            if self.opts.print_test_console_report:
                self._build_results_listener.add(test_node_listener)
            return

        results_dir = self.opts.misc_build_info_dir or self.opts.testenv_report_dir or tempfile.mkdtemp()
        self._output_root = self.opts.output_root or tempfile.mkdtemp()

        tests = (
            [t for t in self.ctx.tests if t.is_skipped()] if self.opts.report_skipped_suites_only else self.ctx.tests
        )
        self._report = results_report.StoredReport()
        report_list = [self._report]
        if self.opts.streaming_report_url or self.opts.report_to_ci:
            # streaming_client is not available in OSS version of the ya
            from yalibrary import streaming_client as sc

            if self.opts.report_to_ci:
                adapter = sc.StreamingCIAdapter(
                    self.opts.ci_logbroker_token,
                    self.opts.ci_topic,
                    self.opts.ci_source_id,
                    self.opts.ci_check_id,
                    self.opts.ci_check_type,
                    self.opts.ci_iteration_number,
                    self.opts.stream_partition,
                    self.opts.ci_task_id_string,
                    self.opts.streaming_task_id,
                    self.opts.ci_logbroker_partition_group,
                    use_ydb_topic_client=self.opts.ci_use_ydb_topic_client,
                )
            else:
                adapter = sc.StreamingHTTPAdapter(
                    self.opts.streaming_report_url,
                    self.opts.streaming_report_id,
                    self.opts.stream_partition,
                    self.opts.streaming_task_id,
                )
            report_list.append(
                results_report.AggregatingStreamingReport(
                    self.targets,
                    sc.StreamingClient(adapter, self.opts.streaming_task_id),
                    self.opts.report_config_path,
                    self.opts.keep_alive_streams,
                    self.opts.report_only_stages,
                )
            )

        if self.opts.json_line_report_file:
            report_list.append(results_report.JsonLineReport(self.opts.json_line_report_file))

        self._reports_generator = ar.ReportGenerator(
            self.opts,
            self.distbuild_graph,
            self.targets,
            tests,
            self.get_owners(),
            self.get_make_files(),
            results_dir,
            self._output_root,
            report_list,
        )
        test_node_listener.set_report_generator(self._reports_generator)
        self._build_results_listener.add(test_node_listener)
        self._build_results_listener.add(
            pr.BuildResultsListener(
                self.ctx.graph,
                tests,
                self._reports_generator,
                test_results_path,
                self.opts,
            )
        )
        if self.opts.use_distbuild:
            self._slot_time_listener = pr.SlotListener(self.opts.statistics_out_dir)
            self._build_results_listener.add(self._slot_time_listener)

        if not self.opts.report_skipped_suites_only:
            self._reports_generator.add_configure_results(self.ctx.configure_errors)

        if self.opts.report_skipped_suites or self.opts.report_skipped_suites_only:
            self._reports_generator.add_tests_results(
                self.ctx.stripped_tests, build_errors=None, node_build_errors_links=[]
            )

        if len(tests) == 0 and (not self.opts.report_skipped_suites and not self.opts.report_skipped_suites_only):
            self._reports_generator.finish_style_report()
            self._reports_generator.finish_tests_report()

        self._reports_generator.finish_configure_report()

    def _calc_exit_code(self):
        # Configuration errors result in an exit with error code 8
        # Test execution errors result in an exit with error code 10
        # If both errors are present (mode -k / --keep-going), the system exits with exit code 8
        # in order to separate standard test execution errors from test errors associated with configuration errors.
        # TODO remove ignore_configure_errors check when YA-1456 is done
        if not self.opts.ignore_configure_errors and self.ctx.configure_errors:
            return self.exit_code or devtools.ya.core.error.ExitCodes.CONFIGURE_ERROR

        if self.ctx.threads:
            # XXX Don't try to inspect test statuses in listing mode.
            # Listing mode uses different test's info data channel and doesn't set proper test status
            if self.opts.run_tests and not self.opts.list_tests:
                if self.ctx.tests:
                    return self.exit_code or get_suites_exit_code(self.ctx.tests, self.opts.test_fail_exit_code)
                else:
                    # TODO Remove --no-tests-is-error option and fail with NO_TESTS_COLLECTED exit code by default. For more info see YA-1087
                    # Return a special exit code if tests were requested, but no tests were run.
                    if self.opts.no_tests_is_error:
                        return self.exit_code or devtools.ya.core.error.ExitCodes.NO_TESTS_COLLECTED

        return self.exit_code

    def _test_console_report(self):
        suites = self.ctx.tests + self.ctx.stripped_tests

        broken_deps = False
        if self.exit_code:
            # Build is failed - search for broken deps for tests
            broken_deps = self.set_skipped_status_for_broken_suites(suites)

        test_results_console_printer.print_tests_results_to_console(self, suites)
        # Information about the error in the suite may be too high and not visible - dump explicit message after the report
        if broken_deps:
            self.app_ctx.display.emit_message("[[alt3]]SOME TESTS DIDN'T RUN DUE TO BUILD ERRORS[[rst]]")

    def _get_results_root(self):
        results_root = test_tools.get_results_root(self.opts)
        return results_root and results_root.replace("$(SOURCE_ROOT)", self.ctx.src_dir)

    def _finish_reports(self):
        if self._slot_time_listener:
            self._slot_time_listener.finish()

        if self._reports_generator is None:
            return

        logger.debug('Build is finished, process results')
        if not self.opts.report_skipped_suites_only:
            self._reports_generator.add_build_results(self.build_result)
        self._reports_generator.finish_build_report()

        suites = []
        if self.opts.report_skipped_suites_only or self.opts.report_skipped_suites:
            suites = self.ctx.stripped_tests
        if not self.opts.report_skipped_suites_only:
            suites += build_report.fill_suites_results(self, self.ctx.tests, self._output_root)

        report_prototype = collections.defaultdict(list)

        self._reports_generator.add_tests_results(
            suites, self.build_result.build_errors, self.build_result.node_build_errors_links, report_prototype
        )
        self._reports_generator.finish_tests_report()
        self._reports_generator.finish_style_report()
        self._reports_generator.finish()
        if self._build_results_listener:
            logger.debug("Build results listener statistics: %s", self._build_results_listener.stat)

    def make_report(self):
        return self._report.make_report() if self._report is not None else None

    def go(self):
        self._setup_build_root(self.opts.bld_root)
        self._setup_reports()
        self._setup_compact_for_gc()

        if not self.ctx.threads:
            with stager.scope('finalize-reports'):
                self._finish_reports()
                self._generate_report()

            self._strip_cache(self.ctx.threads)
            release_all_data()

            return self._calc_exit_code()

        if exts.windows.on_win() and self.opts.create_symlinks and not self.opts.output_root:
            logger.warning(
                "Symlinks for the outputs are disabled on Windows. Use -o/--output option to explicitly specify the directory for the outputs"
            )

        try:
            result_analyzer = None  # using for --stat
            if self.opts.report_skipped_suites_only:
                self.exit_code = 0
            else:
                if self.ctx.graph and len(self.ctx.graph['graph']) == 0:
                    (
                        res,
                        err,
                        err_links,
                        tasks_metrics,
                        self.exit_code,
                        node_status_map,
                        exit_code_map,
                        result_analyzer,
                    ) = ({}, {}, {}, {}, 0, {}, {}, None)
                else:
                    (
                        res,
                        err,
                        err_links,
                        tasks_metrics,
                        self.exit_code,
                        node_status_map,
                        exit_code_map,
                        result_analyzer,
                    ) = self._dispatch_build(self._build_results_listener)
                (
                    errors,
                    errors_links,
                    failed_deps,
                    node_build_errors,
                    node_build_errors_links,
                ) = br.make_build_errors_by_project(self.ctx.graph['graph'], err, err_links or {})
                build_metrics = st.make_targets_metrics(self.ctx.graph['graph'], tasks_metrics)
                self.build_result = br.BuildResult(
                    errors,
                    failed_deps,
                    node_build_errors,
                    res,
                    build_metrics,
                    errors_links,
                    node_build_errors_links,
                    node_status_map,
                    exit_code_map,
                )

                if self.ctx.cache_test_statuses:
                    with stager.scope('cache_test_statuses'):
                        last_failed.cache_test_statuses(
                            res, self.ctx.tests, self.ctx.garbage_dir, self.opts.last_failed_tests
                        )

            if self.opts.print_test_console_report:
                self._test_console_report()
            if result_analyzer is not None:
                result_analyzer()

            with stager.scope('finalize-reports'):
                self._finish_reports()
                self._generate_report()

            self.exit_code = self._calc_exit_code()

            msg = self._calc_msg(self.exit_code)
            self.app_ctx.display.emit_message(msg)

            if self.ctx.create_symlinks:
                self._setup_repo_state()
                self.ctx.symlink_result.commit()

            if self.ctx.opts.get_deps and self.exit_code == 0:
                get_deps(self.ctx.opts.rel_targets, self.ctx.graph, self.build_result, self.ctx.opts.get_deps)

            if self.opts.dump_raw_results and self.opts.output_root:
                self._dump_raw_build_results(self.opts.output_root)

            if (
                hasattr(self.ctx, 'yt_cached_results')
                and hasattr(self.ctx, 'yt_not_cached_results')
                and self.opts.output_root
            ):
                self._dump_yt_cache_nodes_by_status(self.opts.output_root)

            self._strip_cache(self.ctx.threads)

            return self.exit_code
        finally:
            self.ctx.cache.flush()
            release_all_data()
            post_local_cache_report()
            self.ctx.unlock()

    def _calc_msg(self, exit_code):
        if exit_code == devtools.ya.core.error.ExitCodes.NO_TESTS_COLLECTED:
            return "[[bad]]Failed - No tests collected[[rst]]"
        elif exit_code:
            return '[[bad]]Failed[[rst]]'
        elif self.opts.show_final_ok:
            return '[[good]]Ok[[rst]]'
        return ''  # clear stderr

    def _validate_opts(self):
        if self.opts.build_results_report_file and not self.opts.output_root:
            raise devtools.ya.core.yarg.FlagNotSupportedException(
                "--build-results-report must be used with not empty --output"
            )
        if self.opts.junit_path and not self.opts.output_root:
            raise devtools.ya.core.yarg.FlagNotSupportedException("--junit must be used with not empty --output")
        # Use os.path.commonpath when YA-71 is done (`import six` - keywords for simplifying the search for technical debt)
        if self.opts.output_root:
            abs_output = os.path.normpath(os.path.abspath(self.opts.output_root))
            abs_root = os.path.normpath(os.path.abspath(self.arc_root))
            if exts.fs.commonpath([abs_root, abs_output]).startswith(abs_root):
                self.app_ctx.display.emit_message(
                    '[[warn]]Output root is subdirectory of Arcadia root, this may cause non-idempotent build[[rst]]'
                )

    def _dispatch_build(self, callback):
        with stager.scope('dispatch_build'):
            if self.opts.use_distbuild:
                return self._build_distbs(callback)
            else:
                return self._build_local(callback)

    def _build_distbs(self, callback):
        from devtools.ya.yalibrary.yandex.distbuild import distbs

        display = self.app_ctx.display

        patterns = ptrn.Patterns()
        patterns['SOURCE_ROOT'] = exts.path2.normpath(self.ctx.src_dir)

        if self.opts.ymake_bin and any(
            [self.opts.make_context_on_distbuild, self.opts.make_context_on_distbuild_only, self.opts.make_context_only]
        ):
            raise devtools.ya.core.yarg.FlagNotSupportedException(
                "Context generation on distbuild with specified ymake binary is not supported yet"
            )

        if not self.opts.download_artifacts:
            logger.warning(
                'Distributed build does not download build and testing artifacts by default. '
                + 'Use -E/--download-artifacts option, see details in help.'
            )

        def upload_resources_to_sandbox():
            for resource in self.ctx.graph.get('conf', {}).get('resources', []):
                if resource.get('resource', '').startswith("file:"):
                    import yalibrary.upload.uploader as uploader

                    logger.debug('Uploading resource ' + resource['resource'])
                    resource['resource'] = 'sbr:' + str(
                        uploader.do(paths=[os.path.abspath(resource['resource'][len("file:") :])], ttl=1)
                    )
                    logger.debug('Uploaded to ' + resource['resource'])

        def upload_repository_package():
            import devtools.ya.build.source_package as sp

            self.app_ctx.display.emit_status('Start to pack and upload inputs package')
            inputs_map = self.ctx.graph['inputs']
            for node in self.ctx.graph['graph']:
                node_inputs = {i: None for i in node['inputs']}
                inputs_map = lg.union_inputs(inputs_map, node_inputs)
            repository_package = sp.pack_and_upload(self.opts, inputs_map, self.opts.arc_root)
            self.ctx.graph['conf']['repos'] = gp.make_tared_repositories_config(repository_package)

        def run_db():
            ready = None
            try:
                upload_resources_to_sandbox()

                if self.opts.repository_type == distbs_consts.DistbuildRepoType.TARED:
                    upload_repository_package()

                if self.opts.dump_distbuild_graph:
                    with zcopen(self.opts.dump_distbuild_graph) as graph_file:
                        sjson.dump(self.ctx.full_graph, graph_file)

                def activate_callback(res=None, build_stage=None):
                    try:
                        for x in res or []:
                            self.ctx.task_context.dispatch_uid(x['uid'], x)
                            if callback:
                                callback(x)
                        if build_stage and callback:
                            callback(build_stage=build_stage)
                    except Exception as e:
                        logger.debug(
                            "Failed to activate: %s, exception %s",
                            ([x['uid'] for x in res] if res else build_stage),
                            str(e),
                        )
                        raise

                logger.debug("Waiting local executor to prepare")
                ready = self.ctx.local_runner_ready.wait()
                logger.debug("Local executor status: %s", str(ready))
                if ready:
                    logger.debug("Starting distbuild")
                    res = distbs.run_dist_build(
                        self.ctx.release_full_graph(),
                        self.ctx.opts,
                        display,
                        activate_callback,
                        output_replacements=self.ctx.output_replacements,
                        evlog=getattr(self.app_ctx, 'evlog', None),
                        patterns=patterns,
                    )
                else:
                    logger.debug("Exit from local runner by exception. No local executor, hence no distbuild started")
                    res = collections.defaultdict(list)

                if self.opts.dump_distbuild_result:
                    with open(self.opts.dump_distbuild_result, 'w') as result_file:
                        json.dump(res, result_file)

                return res

            finally:
                if ready is None:
                    ready = self.ctx.local_runner_ready.wait()
                # Even if 'not ready', local executor may be partially initialized.
                if hasattr(self.ctx, 'task_context'):
                    logger.debug("Cleanup local executor: dispatch_all tasks, ready=%s", ready)
                    self.ctx.task_context.dispatch_all()
                else:
                    # ready should be False here
                    logger.debug("Local executor malfunction: task_context is not prepared, ready=%s", ready)

        def extract_exit_code_or_status(db_result):
            errors = list(db_result.get('failed_results', {}).get('reasons', {}).values())
            for x in errors:
                logger.debug("Failed result: %s", json.dumps(x, sort_keys=True))
            return max(max(item['exit_code'], item['status'] != 0) for item in errors) if errors else 0

        def extract_tasks_metrics(db_result):
            return db_result.get('tasks_metrics', {})

        def extract_build_errors(db_result):
            extract_build_errors_stage = stager.start('extract_build_errors')

            import exts.asyncthread

            def fetch_one(kv, mds_read_account=self.opts.mds_read_account):
                u, desc = kv
                stderr, links = distbs.extract_stderr(
                    desc, mds_read_account, download_stderr=self.opts.download_failed_nodes_stderr
                )
                return (
                    (u, patterns.fix(six.ensure_str(stderr))),
                    (u, links),
                    (u, desc['status']),
                    (u, desc.get('exit_code', 0)),
                )

            net_threads = self.ctx.opts.yt_store_threads + self.ctx.opts.dist_store_threads
            default_download_thread_count = self.ctx.threads + net_threads
            download_thread_count = self.opts.stderr_download_thread_count or default_download_thread_count
            logger.debug(
                "Default download thread count is %i (%i + %i), actual is %i",
                default_download_thread_count,
                self.ctx.threads,
                net_threads,
                download_thread_count,
            )

            failed_items = list(db_result.get('failed_results', {}).get('reasons', {}).items())

            fetch_results = tuple(zip(*exts.asyncthread.par_map(fetch_one, failed_items, download_thread_count)))

            stderr_pairs, links_pairs, status_pairs, exit_code_map = fetch_results or ([], [], [], [])

            extract_build_errors_stage.finish()
            return dict(stderr_pairs), dict(links_pairs), dict(status_pairs), dict(exit_code_map)

        run_db_async = core_async.future(run_db, daemon=False)

        try:
            res, err, exit_code, local_execution_log, _ = self.ctx.runner(
                self.ctx, self.app_ctx, callback, output_replacements=self.ctx.output_replacements
            )
        finally:
            distbs_result = run_db_async()

        def result_analyzer():
            return bs.analyze_distbuild_result(
                distbs_result,
                self.distbuild_graph.get_graph(),
                self.opts.statistics_out_dir,
                self.opts,
                get_task_stats(),
                local_result=local_execution_log,
                ctx_stages=self.ctx.stage_times,
                ymake_stats=self.ctx.ymake_stats,
            )

        build_errors, build_errors_links, status_map, exit_code_map = extract_build_errors(distbs_result)
        return (
            res,
            build_errors,
            build_errors_links,
            extract_tasks_metrics(distbs_result),
            extract_exit_code_or_status(distbs_result),
            status_map,
            exit_code_map,
            result_analyzer,
        )

    def _build_local(self, callback):
        res, errors, exit_code, execution_log, exit_code_map = self.ctx.runner(
            self.ctx, self.app_ctx, callback, output_replacements=self.ctx.output_replacements
        )

        def extract_tasks_metrics():
            metrics = {}
            for u, data in execution_log.items():
                if 'timing' in data and data['timing']:
                    elapsed = data['timing'][1] - data['timing'][0]
                    metrics[u] = {'elapsed': elapsed}
            return metrics

        def result_analyzer():
            return bs.analyze_local_result(
                execution_log,
                self.ctx.graph,
                self.opts.statistics_out_dir,
                self.opts,
                set(errors.keys()),
                get_task_stats(),
                ctx_stages=self.ctx.stage_times,
                ymake_stats=self.ctx.ymake_stats,
            )

        if not self.opts.random_build_root:
            latest_path = os.path.join(self.opts.bld_dir, 'latest')
            if os.path.islink(latest_path):
                exts.fs.remove_file(latest_path)
            if not os.path.exists(latest_path):
                exts.fs.symlink(self.opts.bld_root, latest_path)

        return res, errors, None, extract_tasks_metrics(), exit_code, {}, exit_code_map, result_analyzer

    @property
    def targets(self):
        return self.distbuild_graph.get_targets()

    @func.lazy_property
    def distbuild_graph(self):
        return bp.DistbuildGraph(self.ctx.graph)

    def _dump_raw_build_results(self, dest):
        try:
            fd = {str(k): v for k, v in self.build_result.failed_deps.items()}
            self._dump_json(os.path.join(dest, 'failed_dependants.json'), fd)
            self._dump_json(os.path.join(dest, 'configure_errors.json'), self.ctx.configure_errors)
            self._dump_json(os.path.join(dest, 'ok_nodes.json'), self.build_result.ok_nodes)
            self._dump_json(os.path.join(dest, 'targets.json'), self.targets)
            be = {str(k): v for k, v in self.build_result.build_errors.items()}
            self._dump_json(os.path.join(dest, 'build_errors.json'), be)
        except Exception:
            logging.exception("Error while dumping raw results")

    def _dump_yt_cache_nodes_by_status(self, dest):
        for name in ('yt_cached_results', 'yt_not_cached_results'):
            with open(os.path.join(dest, name + '.txt'), 'w') as f:
                f.write('\n'.join(getattr(self.ctx, name, [])))

    def set_skipped_status_for_broken_suites(self, suites):
        broken_deps = False
        for suite in suites:
            if not suite._errors and not suite.tests:
                suite.add_suite_error("[[bad]]skipped due to a failed build[[warn]]", const.Status.SKIPPED)
                broken_deps = True
        return broken_deps


def merge_pgo_profiles(profiles):
    logger.info('Will try to merge pgo profiles. It can take some time.')

    all_profiles = [g for profile in profiles for g in glob.glob(profile)]
    if not all_profiles:
        raise EmptyProfilesListException('PGO profiles not found')

    if len(all_profiles) == 1:
        with open(all_profiles[0], 'rb') as f:
            if f.read(8) == b'\xfflprofi\x81':
                return all_profiles[0]
    hashes = [hashing.fast_filehash(fname) for fname in all_profiles]
    output = ''
    for h in sorted(hashes):
        output = hashing.fast_hash(output + h)
    output = 'merged.' + output + '.profdata'
    if not os.path.exists(output):
        subprocess.check_call([str(tools.tool('llvm-profdata')), 'merge', '-output', output] + all_profiles)
    logger.info('pgo merged profile name is %s', os.path.abspath(output))
    return output
