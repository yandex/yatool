from collections import defaultdict

import copy
import functools
import itertools
import threading
import exts.yjson as json
import logging
import os
import re
import sys
import time
import tempfile

from exts import tmp
import exts.process
import exts.strings
import exts.archive
import exts.fs
import exts.windows

import app_config
import yalibrary.tools as tools

import devtools.ya.core.error as core_error
import devtools.ya.core.yarg as yarg
import devtools.ya.core.config
import devtools.ya.core.report
import devtools.ya.core.event_handling

import devtools.ya.build.genconf as genconf
import devtools.ya.build.prefetch as prefetch
from devtools.ya.build.ymake2 import consts
from devtools.ya.build.ymake2 import run_ymake
from devtools.ya.build.ccgraph.cpp_string_wrapper import CppStringWrapper

from devtools.ya.build.evlog.progress import PrintProgressSubscriber


logger = logging.getLogger(__name__)


class YMakeError(Exception):
    mute = True

    def __init__(self, err, exit_code=None):
        super().__init__(err)
        self.exit_code = exit_code


class YMakeConfigureError(YMakeError):
    mute = True


class YMakeNeedRetryError(YMakeError):
    def __init__(self, err, dirty_dirs):
        super(YMakeError, self).__init__(err)
        self.dirty_dirs = dirty_dirs


class YMakeResult:
    def __init__(self, exit_code, stdout, stderr, meta=None):
        self.exit_code = exit_code
        self.stdout = stdout
        self.stderr = stderr
        try:
            self.meta = json.loads(meta)
        except (ValueError, TypeError):
            self.meta = None

    def get_meta_value(self, key, default_value=''):
        if self.meta is not None and key in self.meta:
            return self.meta[key]
        return default_value


def _get_ev_listener_param():
    try:
        import app_ctx

        return app_ctx.event_queue
    except ImportError:
        return lambda _: _


def _ymake_params(check=True):
    return [
        yarg.Param(name='ymake_bin', default_value=None),
        yarg.Param(name='use_local_conf', default_value=True),
        yarg.Param(name='check', default_value=check),
        yarg.Param(name='extra_env', default_value=None),
        yarg.Param(name='mode', default_value=None),  # XXX: remove me
        yarg.Param(name='ev_listener', default_value=_get_ev_listener_param()),
        yarg.Param(name='_purpose', default_value='UNKNOWN'),
    ]


def _global_params(check):
    return _ymake_params(check=check) + [
        yarg.Param(name='be_verbose', default_value=False),
        yarg.Param(name='warn_mode', default_value=[]),
        yarg.Param(name='dump_meta', default_value=None),
        yarg.Param(name='debug_options', default_value=[]),
        yarg.Param(name='flags', default_value={}),
        yarg.Param(name='dump_file', default_value=None),
    ]


def _configure_params(buildable, build_type=None, continue_on_fail=False, check=True):
    return _global_params(check=check) + [
        yarg.Param('custom_build_directory', default_value=None),
        yarg.Param('abs_targets', default_value=[]),
        yarg.Param('build_type', default_value=build_type),
        yarg.Param('continue_on_fail', default_value=continue_on_fail),
        yarg.Param('dump_info', default_value=None),
        yarg.Param('custom_conf', default_value=None),
        yarg.Param('no_ymake_resource', default_value=False),
        yarg.Param('build_depends', default_value=False),
        yarg.Param('dump_make_files', default_value=None),
        yarg.Param('dump_tests', default_value=None),
        yarg.Param('checkout_data_by_ya', default_value=False),
        yarg.Param('dump_java', default_value=None),
        yarg.Param('disable_customization', default_value=False),
        yarg.Param('source_root', default_value=None),
        yarg.Param('stdin_line_provider', default_value=None),
        yarg.Param('targets_from_evlog', default_value=False),
        yarg.Param('transition_source', default_value=None),
        yarg.Param('report_pic_nopic', default_value=None),
        yarg.Param('descend_into_foreign', default_value=None),
        yarg.Param('drop_foreign_start_modules', default_value=None),
        yarg.Param('multiconfig', default_value=False),
        yarg.Param('order', default_value=None),
        yarg.Param('dont_check_transitive_requirements', default_value=None),
    ]


def _build_params():
    return _configure_params(buildable=True, check=False) + [
        yarg.Param('clear_build', default_value=False),
        yarg.Param('no_caches_on_retry', default_value=False),
    ]


def _gen_graph_params():
    return (
        _configure_params(buildable=False, continue_on_fail=True)
        + [
            yarg.Param('dump_inputs_map', default_value=False),
            yarg.Param('strict_inputs', default_value=False),
            yarg.Param('dump_graph', default_value=None),
            yarg.Param('find_path_from', default_value=None),
            yarg.Param('find_path_to', default_value=None),
            yarg.Param('managed_dep_tree', default_value=None),
            yarg.Param('classpaths', default_value=None),
            yarg.Param('enabled_events', default_value=consts.YmakeEvents.ALL.value),
            yarg.Param('yndex_file', default_value=None),
            yarg.Param('patch_path', default_value=None),
            yarg.Param('cache_info_file', default_value=None),
            yarg.Param('cache_info_name', default_value=None),
            yarg.Param('modules_info_file', default_value=None),
            yarg.Param('modules_info_filter', default_value=None),
            yarg.Param('lic_link_type', default_value=None),
            yarg.Param('lic_custom_tags', default_value=[]),
            yarg.Param('no_caches_on_retry', default_value=False),
            yarg.Param('no_ymake_retry', default_value=False),
            yarg.Param('cpp', default_value=False),
            yarg.Param('compress_ymake_output_codec', default_value=None),
        ]
        + [
            yarg.Param(name='changelist_generator', default_value=None),
        ]
    )


def _sem_graph_params():
    return _build_params() + [
        yarg.Param('dump_sem_graph', default_value=None),
        yarg.Param('dump_raw_graph', default_value=None),
        yarg.Param('foreign_on_nosem', default_value=None),
        yarg.Param('enabled_events', default_value=consts.YmakeEvents.ALL.value),
    ]


def _ymake_build(**kwargs):
    logger.debug('Run build with %s', kwargs)
    return yarg.behave(kwargs, yarg.Behaviour(action=_prepare_and_run_ymake, params=_build_params()))


def ymake_sem_graph(**kwargs):
    logger.debug('Run sem-graph with %s', kwargs)
    return yarg.behave(kwargs, yarg.Behaviour(action=_prepare_and_run_ymake, params=_sem_graph_params()))


def ymake_dump(**kwargs):
    logger.debug('Run dump with %s', kwargs)
    return yarg.behave(
        kwargs,
        yarg.Behaviour(
            action=_prepare_and_run_ymake,
            params=_gen_graph_params(),
        ),
    )


def ymake_gen_graph(**kwargs):
    logger.debug('Run gen graph with %s', kwargs)

    res, evlog = yarg.behave(kwargs, yarg.Behaviour(action=_prepare_and_run_ymake, params=_gen_graph_params()))

    if app_config.in_house:
        import yalibrary.diagnostics as diag

        if diag.is_active():
            diag.save('ymake-build-json', stdout=res.stdout, stderr=res.stderr)

    return res, evlog


def _cons_ymake_args(**kwargs):
    parse_evlog_file = kwargs.pop('parse_evlog_file', None)
    if parse_evlog_file:
        return ['--evlogdump', parse_evlog_file]

    ret = []

    # GLOBAL PARAMS
    be_verbose = kwargs.pop('be_verbose', False)
    if be_verbose:
        ret += ['--verbose']

    # XXX: remove me
    mode = kwargs.pop('mode', None)
    build_depends = kwargs.pop('build_depends', None)
    if build_depends:
        ret += ['--depends-like-recurse']

    warn_mode = kwargs.pop('warn_mode', [])
    if warn_mode:
        ret += ['--warn', ','.join(warn_mode)]

    dump_meta = kwargs.pop('dump_meta', None)
    if dump_meta:
        ret += ['--write-meta-data', dump_meta]

    custom_conf = kwargs.pop('custom_conf', None)
    if custom_conf:
        ret += ['--config', custom_conf]

    plugins_dir = kwargs.pop('plugins_dir', None)
    if plugins_dir:
        ret += ['--plugins-root', plugins_dir]

    debug_options = kwargs.pop('debug_options', [])
    if debug_options:
        ret += ['--x' + f for f in debug_options]

    dump_file = kwargs.pop('dump_file', None)
    if dump_file:
        ret += ['--dump-file', dump_file]

    patch_path = kwargs.pop('patch_path', None)
    if patch_path:
        ret += ['--patch-path', patch_path]

    cache_info_file = kwargs.pop('cache_info_file', None)
    if cache_info_file:
        ret += ['--cache-info-file', cache_info_file]

    cache_info_name = kwargs.pop('cache_info_name', None)
    if cache_info_name:
        ret += ['--cache-info-name', cache_info_name]

    no_ymake_resource = kwargs.pop('no_ymake_resource', False)
    if no_ymake_resource:
        ret += ['-DNO_YMAKE=yes']

    # CONFIGURE PARAMS
    custom_build_directory = kwargs.pop('custom_build_directory', None)
    if custom_build_directory:
        ret += ['--build-root', custom_build_directory]

    output_dir = kwargs.pop('output_dir', None)  # XXX
    if output_dir:
        ret += ['--build-root', output_dir]

    source_root = kwargs.pop('source_root', None)
    if source_root:
        ret += ['--source-root', source_root]

    targets_from_evlog = kwargs.pop('targets_from_evlog', False)
    if targets_from_evlog:
        ret += ['--targets-from-evlog']

    transition_source = kwargs.pop('transition_source', None)
    if transition_source:
        ret += ['--transition-source', transition_source]

    report_pic_nopic = kwargs.pop('report_pic_nopic', None)
    if report_pic_nopic is not None:
        ret += ['--report-pic-nopic', report_pic_nopic]

    descend_into_foreign = kwargs.pop('descend_into_foreign', None)
    if descend_into_foreign is not None:
        ret += ['--descend-into-foreign', descend_into_foreign]

    drop_foreign_start_modules = kwargs.pop('drop_foreign_start_modules', None)
    if drop_foreign_start_modules is not None:
        ret += ['--drop-foreign-start-modules', drop_foreign_start_modules]

    dont_check_transitive_requirements = kwargs.pop('dont_check_transitive_requirements', False)
    if dont_check_transitive_requirements:
        ret += ['--dont-check-transitive-requirements']

    continue_on_fail = kwargs.pop('continue_on_fail', False)
    if continue_on_fail:
        ret += ['--keep-on']

    dump_info = kwargs.pop('dump_info', [])
    if dump_info:
        ret += ['--dump-custom-data', ';'.join(dump_info)]
        ret += ['--xx']  # XXX

    clear_build = kwargs.pop('clear_build', False)
    if clear_build:
        ret += ['--xx']  # XXX

    dump_test_dart = kwargs.pop('dump_tests', None)
    check_data_paths = kwargs.pop('checkout_data_by_ya', False)
    if dump_test_dart:
        ret += ['--test-dart', dump_test_dart]
        if check_data_paths:
            ret += ['--check-data-paths']

    dump_java_dart = kwargs.pop('dump_java', None)
    if dump_java_dart:
        ret += ['--java-dart', dump_java_dart]

    dump_make_files_dart = kwargs.pop('dump_make_files', None)
    if dump_make_files_dart:
        ret += ['--makefiles-dart', dump_make_files_dart]

    dump_yndex = kwargs.pop('yndex_file', None)
    if dump_yndex:
        ret += ['-Y', dump_yndex]

    dump_sem_graph = kwargs.pop('dump_sem_graph', None)
    if dump_sem_graph:
        ret += ['--sem-graph']

    dump_raw_graph = kwargs.pop('dump_raw_graph', None)
    if dump_raw_graph:
        ret += ['--xg', '--dump-file', dump_raw_graph]

    foreign_on_nosem = kwargs.pop('foreign_on_nosem', None)
    if foreign_on_nosem:
        ret += ['--foreign-on-nosem']

    # GENGRAPH PARAMS
    # XXX
    dump_graph = kwargs.pop('dump_graph', None)
    if dump_graph:
        if mode == 'dist':
            ret += ['--dump-build-plan', '-']
            strict_inputs = kwargs.pop('strict_inputs', False)
            if strict_inputs:
                ret += ['--add-inputs']
            dump_inputs_map = kwargs.pop('dump_inputs_map', False)
            if dump_inputs_map:
                ret += ['--add-inputs-map']
        else:
            ret += ['--xg', '--xJ'] if dump_graph == 'json' else ['--xg']
        json_compression_codec = kwargs.pop('json_compression_codec', None)
        if json_compression_codec:
            ret += ['--json-compression-codec', json_compression_codec]
    else:
        kwargs.pop('strict_inputs', False)
        kwargs.pop('dump_inputs_map', False)

    # Dump info parms
    modules_info_file = kwargs.pop('modules_info_file', None)
    if modules_info_file:
        ret += ['--modules-info-file', modules_info_file]

    modules_info_filter = kwargs.pop('modules_info_filter', None)
    if modules_info_filter:
        ret += ['--modules-info-filter', modules_info_filter]

    lic_link_type = kwargs.pop('lic_link_type', None)
    if lic_link_type:
        ret += ['--xlic-link-type', lic_link_type]

    for tag in kwargs.pop('lic_custom_tags', []):
        ret += ['--xlic-custom-tag', tag]

    find_path_from = kwargs.pop('find_path_from', None)
    if find_path_from:
        for target in find_path_from:
            ret += ['--find-path-from', target]

    find_path_to = kwargs.pop('find_path_to', None)
    if find_path_to:
        for target in find_path_to:
            ret += ['--find-path-to', target]

    managed_dep_tree = kwargs.pop('managed_dep_tree', None)
    if managed_dep_tree:
        for target in managed_dep_tree:
            ret += ['--managed-dep-tree', target]

    classpaths = kwargs.pop('classpaths', None)
    if classpaths:
        for target in classpaths:
            ret += ['--managed-deps', target]

    # TODO: remove these unused options
    kwargs.pop('flags', None)
    kwargs.pop('use_local_conf', None)
    kwargs.pop('build_type', None)

    if kwargs:
        # TODO: raise exception
        logger.warning('Found unused args for ymake: %s', kwargs)

    return list(map(str, ret))


def run_ymake_build(**kwargs):
    if kwargs.get('use_local_conf', None):
        arc_root = devtools.ya.core.config.find_root_from(kwargs.get('abs_targets', None))
        if arc_root:
            genconf.check_local_ymake(os.path.join(arc_root, 'local.ymake'))
    return _ymake_build(**kwargs)


def _prepare_and_run_ymake(**kwargs):
    if kwargs['custom_conf'] is None:
        # XXX
        logger.debug('XXX Generate fake custom_conf')
        host_platform = genconf.host_platform_name()
        toolchain_params = genconf.gen_tc(host_platform)
        arc_root = devtools.ya.core.config.find_root_from(kwargs['abs_targets'])
        custom_conf, _ = genconf.gen_conf(
            arc_dir=arc_root,
            conf_dir=genconf.detect_conf_root(arc_root, kwargs['custom_build_directory']),
            build_type=kwargs['build_type'],
            use_local_conf=True,
            local_conf_path=None,
            extra_flags=kwargs['flags'],
            tool_chain=toolchain_params,
        )
        kwargs['custom_conf'] = custom_conf

    no_caches_on_retry = kwargs.pop('no_caches_on_retry')
    no_ymake_retry = kwargs.pop('no_ymake_retry', False)

    try:
        return _run_ymake(**kwargs)
    except YMakeNeedRetryError:
        if no_ymake_retry:
            raise
        if no_caches_on_retry:
            logger.debug('Retry ymake without caches')
            rc_flag_value = 'RC=f:w,d:w,j:w'
        else:
            logger.debug('Retry ymake with FS cache only')
            rc_flag_value = 'RC=f:a,d:w,j:w'
        retry_kwargs = copy.copy(kwargs)
        if 'debug_options' not in retry_kwargs:
            retry_kwargs['debug_options'] = []
        retry_kwargs.get('debug_options').append(rc_flag_value)

        retry_kwargs['_purpose'] = retry_kwargs.get('_purpose', "UNKNOWN") + "-retry"

        return _run_ymake(**retry_kwargs)


_gen_ymake_uid = functools.partial(next, itertools.count())


def _run_ymake(**kwargs):
    # This id used for:
    # - Divide ymake runs in `ya dump debug`
    # - Show correct configure progress
    _ymake_unique_run_id = _gen_ymake_uid()

    _stat_info = {}
    _stat_info_preparing = _stat_info['preparing'] = {'start': time.time()}
    _stat_info_execution = _stat_info['execution'] = {}
    _stat_info_postprocessing = _stat_info['postprocessing'] = {}
    _metrics = defaultdict(lambda: defaultdict(int))
    _caches = defaultdict(lambda: {'loaded': False, 'saved': False, 'loading_enabled': False, 'saving_enabled': False})
    _stages = defaultdict(lambda: {'start': None, 'finish': None, 'duration': None})

    _run_info = {  # for devtools.ya.core.report
        'ymake_run_uid': _ymake_unique_run_id,
        'stats': _stat_info,
        'purpose': kwargs.pop('_purpose', None),
        'caches': _caches,
        'stages': _stages,
        "metrics": _metrics,
    }
    _debug_info = {'run': _run_info, 'kwargs': kwargs}  # for dump_debug

    def stages_listener(j):
        try:
            _type = j['_typename']

            if _type in ('NEvent.TStageStarted', 'NEvent.TStageFinished'):
                name = j['StageName']
                ts = j['_timestamp'] / 10**6

                if _type == 'NEvent.TStageStarted':
                    _stages[name]['start'] = ts
                elif _type == 'NEvent.TStageFinished':
                    _stages[name]['finish'] = ts
                    if _stages[name]['start']:
                        _stages[name]['duration'] = ts - _stages[name]['start']
                else:
                    raise NotImplementedError(_type)
            elif _type == "NEvent.TDisplayMessage":
                sub = j['Sub']  # type: str
                msg = j["Message"]  # type: str
                if sub.endswith("stats"):
                    for item in msg.split(";"):  # type: str
                        if not item:
                            continue
                        k, v = item.rsplit("=", 1)
                        k = k.strip()
                        v = v.strip()

                        _metrics[sub.replace("stats", "").strip()][k] += int(v)
                elif "has been" in msg:
                    cache_name, action = msg.strip("\n. ").split("has been")
                    cache_name = cache_name.strip()
                    action = action.strip()

                    _caches[cache_name][action] = True
                elif "cache loading is enabled" in msg or "cache saving is enabled" in msg:
                    m = re.search("^(.+? cache) (loading|saving)", msg)
                    if m:
                        cache_name = m.group(1)
                        action = m.group(2) + '_enabled'
                        _caches[cache_name][action] = True
            elif _type == "NEvent.TGraphChanges":
                _run_info["has_content_changes"] = j.get("HasContentChanges", None)
                _run_info["has_structural_changes"] = j.get("HasStructuralChanges", None)
            elif _type == "NEvent.TGraphChangesPrediction":
                _run_info["predicts_structural_changes"] = j.get("PredictsStructuralChanges", None)
            elif _type == "NEvent.TArcChanges":
                _run_info["has_changelist"] = j.get("HasChangelist", None)
            elif _type == "NEvent.TNodeChanges":
                _run_info["has_rendered_node_changes"] = j.get("HasRenderedNodeChanges", None)
            elif _type == "NEvent.TBypassConfigure":
                _run_info["bypass_is_enabled"] = j.get("MaybeEnabled", None)
                _run_info["bypass_is_triggered"] = j.get("Enabled", None)

        except Exception:
            logger.exception("While processing event: `%s`", j)

    check = kwargs.pop('check')
    ev_listener = kwargs.pop('ev_listener')
    enabled_events = kwargs.pop('enabled_events') if 'enabled_events' in kwargs else consts.YmakeEvents.DEFAULT.value

    events = []
    app_ctx = sys.modules.get("app_ctx")

    def stderr_listener(line):
        try:
            j = json.loads(line)
        except ValueError:
            logger.warning(line)
            return

        j['ymake_run_uid'] = _ymake_unique_run_id
        events.append(j)
        ev_listener(j)
        stages_listener(j)
        if app_ctx:
            app_ctx.state.check_cancel_state()

    meta_data = {}

    arc_root = kwargs.get('source_root', None) or devtools.ya.core.config.find_root_from(
        kwargs.get('abs_targets', None)
    )
    if arc_root:
        plugins_roots = [os.path.join(arc_root, genconf.ymake_build_dir())]
        if not kwargs.pop('disable_customization', False):
            plugins_roots.append(os.path.join(arc_root, genconf.ymake_build_internal_dir()))
        kwargs['plugins_dir'] = ','.join(os.path.join(x, 'plugins') for x in plugins_roots)

    if not kwargs.get('custom_build_directory'):
        kwargs['custom_build_directory'] = tempfile.mkdtemp('yatmpbld')  # XXX: always define from outside

    prefetcher = None
    if app_ctx:
        real_arc_root = os.path.realpath(arc_root)
        if prefetch.prefetch_condition(
            real_arc_root,
            getattr(app_ctx.params, 'prefetch', False),
            getattr(app_ctx, 'vcs_type', ''),
        ):
            prefetcher = prefetch.ArcPrefetchSubscriber.get_subscriber(real_arc_root)

            prefetcher.subscribe_to(app_ctx.event_queue)

        progress_subscriber = PrintProgressSubscriber(app_ctx.params, getattr(app_ctx, 'display', None), logger)
        progress_subscriber.subscribe_to(app_ctx.event_queue)

        cache_info_subscriber = None
        generator = kwargs.pop('changelist_generator', None)
        if generator is not None:
            try:
                import yalibrary.build_graph_cache as bg_cache
            except ImportError:
                pass
            else:
                cache_info_subscriber = bg_cache.CacheInfoSubscriber(
                    generator, _ymake_unique_run_id, kwargs['custom_build_directory']
                )
                app_ctx.event_queue.subscribe(cache_info_subscriber)

    try:
        with tmp.temp_file() as temp_meta:
            binary = kwargs.pop('ymake_bin', None) or _mine_ymake_binary()
            _run_info['binary'] = binary

            custom_build_dir = kwargs.get('custom_build_directory')

            env = kwargs.pop('extra_env', {})
            _debug_info['env'] = env
            targets = kwargs.pop('abs_targets', [])

            if not kwargs.get('dump_meta', None):
                kwargs['dump_meta'] = temp_meta

            is_cpp_consumer = kwargs.pop("cpp", False)

            is_output_compressed = False
            compress_ymake_output_codec = kwargs.pop("compress_ymake_output_codec", None)
            if compress_ymake_output_codec and kwargs.get('dump_graph', None) == 'json':
                kwargs['json_compression_codec'] = compress_ymake_output_codec
                is_output_compressed = True

            stdin_line_provider = kwargs.pop('stdin_line_provider', None)
            multiconfig = kwargs.pop('multiconfig', False)
            order = kwargs.pop('order', None)
            args = (
                _cons_ymake_args(**kwargs)
                + ['--quiet']
                + (['--events', enabled_events] if enabled_events else [])
                + targets
            )
            _run_info['args'] = [binary] + args

            _stat_info_preparing['finish'] = time.time()
            _stat_info_preparing['duration'] = _stat_info_preparing['finish'] - _stat_info_preparing['start']

            _stat_info_execution['start'] = time.time()
            exit_code, stdout, stderr = run_ymake.run(
                binary,
                args,
                env,
                stderr_listener,
                raw_cpp_stdout=is_cpp_consumer,
                stdin_line_provider=stdin_line_provider,
                multiconfig=multiconfig,
                order=order,
            )
            _stat_info_execution['finish'] = time.time()
            _stat_info_execution['duration'] = _stat_info_execution['finish'] - _stat_info_execution['start']

            _run_info['exit_code'] = exit_code
            _run_info['duration'] = _stat_info_execution['duration']  # Backward compatibility with something

            if is_output_compressed and isinstance(stdout, CppStringWrapper):
                stdout.set_compressed(True)

            _stat_info_postprocessing['start'] = time.time()
            if app_config.in_house:
                import yalibrary.diagnostics as diag

                if diag.is_active() and custom_build_dir:
                    with tmp.temp_file() as tar_gz_file:
                        exts.archive.create_tar(
                            custom_build_dir, tar_gz_file, exts.archive.GZIP, exts.archive.Compression.Best
                        )
                        diag.save('ymake-cache', ymake_cache_arch=exts.fs.read_file(tar_gz_file))

            if exit_code != 0 and check:
                logger.debug("ymake stderr: \n%s", stderr)
                _debug_info['stdout'] = stdout
                _debug_info['stderr'] = stderr
                if exit_code == 2:
                    # XXX YA-1456
                    if getattr(app_ctx.params, 'ignore_configure_errors', True):
                        ecode = None
                    else:
                        ecode = core_error.ExitCodes.CONFIGURE_ERROR
                    raise YMakeConfigureError('Configure error (use -k to proceed)', exit_code=ecode)
                if exit_code == 3:
                    raise YMakeNeedRetryError('YMake wants to be retried', [_f for _f in [custom_build_dir] if _f])
                if exit_code < 0:
                    raise YMakeNeedRetryError('YMake crashed', [_f for _f in [custom_build_dir] if _f])
                raise YMakeError('YMake failed with exit code {}'.format(exit_code), exit_code=exit_code)

            if os.path.exists(kwargs['dump_meta']):
                with open(kwargs['dump_meta']) as meta_file:
                    meta_data = meta_file.read()
    except Exception as e:
        _run_info['exception'] = {
            'type': type(e).__name__,
            'msg': str(e),
            'args': list(map(exts.strings.to_str, e.args)),
        }
        raise
    finally:
        if app_ctx and prefetcher is not None:
            prefetcher.unsubscribe_from(app_ctx.event_queue)
        if app_ctx and progress_subscriber is not None:
            progress_subscriber.unsubscribe_from(app_ctx.event_queue)
        if app_ctx and cache_info_subscriber is not None:
            app_ctx.event_queue.unsubscribe(cache_info_subscriber)

        if _stat_info_postprocessing.get('start'):
            _stat_info_postprocessing['finish'] = time.time()
            _stat_info_postprocessing['duration'] = (
                _stat_info_postprocessing['finish'] - _stat_info_postprocessing['start']
            )

        try:
            if app_ctx:
                app_ctx.dump_debug['ymake_run_{}'.format(_ymake_unique_run_id)] = _debug_info
                # Store file
                app_ctx.dump_debug['ymake_run_{}_conf_file'.format(_ymake_unique_run_id)] = kwargs.get(
                    'custom_conf', None
                )
        except Exception:
            logger.exception("While store debug info")

        logger.debug("ymake_run_info: %s", json.dumps(_run_info))
        devtools.ya.core.report.telemetry.report(devtools.ya.core.report.ReportTypes.RUN_YMAKE, _run_info)

    return YMakeResult(exit_code, stdout, stderr, meta_data), events


def run_ymake_scheduled(count):
    # it's crucial to call run_ymake.run_scheduled in a separate thread
    # to ensure that main thread is not blocked in "hard" way
    # and signal handlers continue to work
    thread = threading.Thread(target=run_ymake.run_scheduled, args=(count,))
    thread.start()
    thread.join()


def _mine_ymake_binary():
    suffix = '_dev' if devtools.ya.core.config.is_dev_mode() else ''
    return tools.tool('ymake' + suffix)
