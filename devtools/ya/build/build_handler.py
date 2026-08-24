import logging
import os
import time
import types
import typing

import devtools.ya.core.config as core_config
from devtools.ya.core import stage_tracer, stages_profiler
from devtools.ya.yalibrary import sjson
import exts.fs
from exts.compress import ucopen
from exts.decompress import udopen
import library.python.filelock as filelock
from yalibrary.monitoring import YaMonEvent
import yalibrary.status_view as status_view

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("build_handler")
stage_begin_time = {}
distbuild_mock_data_dir_env = "__DISTBUILD_MOCK_DIR"
YA_MAKE_LOCK_FILE = 'ya_make.lock'
YA_MAKE_LOCK_POLL_INTERVAL = 0.1


class _YaMakeLockWaitTask:
    def __init__(self, owner_pid: typing.Callable[[], str]) -> None:
        self._owner_pid = owner_pid

    def owner_status(self) -> str:
        return f"Another `ya make` is active (PID {self._owner_pid()})"

    def status(self) -> str:
        return f"{self.owner_status()}, waiting..."


class YaMakeLock:
    """Serialize opted-in ya make invocations sharing a cache root."""

    def __init__(
        self,
        path: str,
        display: typing.Any,
        agent_console: typing.Any = None,
        poll_interval: float = YA_MAKE_LOCK_POLL_INTERVAL,
    ) -> None:
        self._lock = filelock.PidFileLock(path)
        self._display = display
        self._agent_console = agent_console
        self._poll_interval = poll_interval
        self._acquired = False

    def __enter__(self) -> 'YaMakeLock':
        wait_status = status_view.Status()
        listener = wait_status.listener()
        wait_task = _YaMakeLockWaitTask(self._owner_pid)
        listener.add(wait_task)
        listener.started(wait_task)
        term_view = status_view.TermView(wait_status, self._display, ninja=False, show_active_progress=False)
        ticker = status_view.TickThrottle(term_view.tick, YA_MAKE_LOCK_POLL_INTERVAL)
        try:
            if self._agent_console is None:
                waiting = self._acquire(wait_status, ticker)
            else:
                with self._agent_console.temporary_activity(wait_status.active):
                    waiting = self._acquire(wait_status, ticker)
        finally:
            listener.finished(wait_task)

        self._acquired = True
        if waiting:
            self._display.emit_status('')
        return self

    def _acquire(self, wait_status: status_view.Status, ticker: status_view.TickThrottle) -> bool:
        waiting = False
        while not self._lock.acquire(blocking=False):
            if not waiting and self._agent_console is not None:
                task, elapsed = wait_status.active()[-1]
                logger.warning("%s, waiting for %.1fs...", task.owner_status(), elapsed)
            ticker.tick()
            waiting = True
            time.sleep(self._poll_interval)
        return waiting

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: types.TracebackType | None,
    ) -> None:
        if self._acquired:
            self._lock.release()
            self._acquired = False

    def _owner_pid(self) -> str:
        try:
            pid = self._lock.info.pid
        except (OSError, ValueError):
            return 'unknown'
        return str(pid) if pid else 'unknown'


def _to_json(data):
    if isinstance(data, dict):
        return {str(k): _to_json(v) for k, v in data.items()}
    if isinstance(data, (set, tuple, list)):
        return [_to_json(v) for v in data]
    return data


def _dump_json(root, filename, content):
    with open(os.path.join(root, filename), 'wb') as fp:
        sjson.dump(content, fp)


def _dump_results(builder, owners, dump_results2_json):
    build_result = builder.build_result

    exts.fs.ensure_dir(builder.misc_build_info_dir)
    _dump_json(builder.misc_build_info_dir, 'failed_dependants.json', _to_json(build_result.failed_deps))
    _dump_json(builder.misc_build_info_dir, 'configure_errors.json', builder.ctx.configure_errors)
    _dump_json(builder.misc_build_info_dir, 'make_files.json', _to_json(builder.get_make_files()))
    _dump_json(builder.misc_build_info_dir, 'build_errors.json', _to_json(build_result.build_errors))
    _dump_json(builder.misc_build_info_dir, 'ok_nodes.json', build_result.ok_nodes)
    _dump_json(builder.misc_build_info_dir, 'owners_list.json', owners)
    _dump_json(builder.misc_build_info_dir, 'targets.json', _to_json(builder.targets))
    if dump_results2_json:
        _dump_json(builder.misc_build_info_dir, 'results2.json', builder.make_report())


def monitoring_stage_started(stage_name):
    stager.start(stage_name)
    stage_begin_time[stage_name] = time.time()


def monitoring_stage_finished(stage_name):
    stager.finish(stage_name)
    if stage_name not in stage_begin_time:
        logger.error("stage_end without stage_begin for '%s'", stage_name)
    else:
        YaMonEvent.send('EYaStats::ContextTime', time.time() - stage_begin_time[stage_name])
        del stage_begin_time[stage_name]

    _, build_finished = stages_profiler.get_stage_timestamps('distbs-worktime')

    if build_finished is not None:
        # NOW (end of ya make) - time when build has finished on distbuild (all nodes in graph)
        YaMonEvent.send('EYaStats::YmakeHandlerTailTimeAfterDistBuildFinish', time.time() - build_finished)

        _, dist_download_finished = stages_profiler.get_stage_timestamps('distbs-download-result')
        if dist_download_finished is not None:
            # DistDownloadTailTime = max(DistDownloadTask.end_time) - time when build has finished on distbuild (all nodes in graph)
            YaMonEvent.send('EYaStats::DistDownloadTailTime', dist_download_finished - build_finished)


def do_ya_make(params):
    import app_ctx  # XXX

    if getattr(params, 'locked', False):
        lock_path = os.path.join(core_config.misc_root(), YA_MAKE_LOCK_FILE)
        with YaMakeLock(lock_path, app_ctx.display, agent_console=getattr(app_ctx, 'agent_ui', None)):
            return _do_ya_make(params, app_ctx)
    return _do_ya_make(params, app_ctx)


def _do_ya_make(params, app_ctx):
    from devtools.ya.build import ya_make

    distbuild_mock = None
    if distbuild_mock_data_dir := os.environ.get(distbuild_mock_data_dir_env):
        from devtools.ya.build.distbuild_mock import DistbuildMock

        distbuild_mock = DistbuildMock(distbuild_mock_data_dir)

    monitoring_stage_started('ya_make_handler')
    context_generating_stage = stager.start('context_generating')

    if not params.custom_context:
        with stager.scope("build_graph_cache_configuration"):
            ya_make.configure_build_graph_cache_dir(app_ctx, params)

    # XXX
    if getattr(params, 'make_context_on_distbuild_only', False) or getattr(params, 'make_context_only', False):
        from devtools.ya.build.remote import remote_graph_generator

        remote_graph_generator.generate(params, app_ctx)
        context_generating_stage.finish()
        monitoring_stage_finished('ya_make_handler')
        return 0

    # XXX
    if getattr(params, 'make_context_on_distbuild', False):
        from devtools.ya.build.remote import remote_graph_generator

        context = remote_graph_generator.generate(params, app_ctx)
        builder = context.builder
    elif params.custom_context:
        with udopen(params.custom_context, 'rb') as custom_context_file:
            custom_context_json = sjson.load(custom_context_file, intern_keys=True, intern_vals=True)
        context = ya_make.BuildContext.load(params, app_ctx, custom_context_json)
        builder = context.builder
        del custom_context_json
    else:
        builder = ya_make.YaMake(params, app_ctx)
        context = None

    context_generating_stage.finish()

    exit_code = 0
    if getattr(params, 'save_context_to', None) is None:
        with stager.scope('build'):
            exit_code = builder.go()
        if builder.misc_build_info_dir:
            with stager.scope('dump_results'):
                context = context or ya_make.BuildContext(builder)
                _dump_results(builder, context.owners, params.dump_results2_json)
    else:
        stager.start('save_context')
        with stager.scope('create_build_context'):
            context = context or ya_make.BuildContext(builder)

        context_json = context.save()
        graph_json = context_json.pop('graph')

        with ucopen(params.save_context_to, mode="wb") as context_file:
            sjson.dump(context_json, context_file)

        with ucopen(params.save_graph_to, mode="wb") as graph_file:
            sjson.dump(graph_json, graph_file)

        stager.finish('save_context')

    monitoring_stage_finished('ya_make_handler')

    if distbuild_mock:
        distbuild_mock.close()

    return 0 if params.ignore_nodes_exit_code else exit_code
