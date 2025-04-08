import collections
import contextlib
import exts.yjson as json
import fnmatch
import logging
import os
import re
import time
import types
import weakref

import typing as tp
from collections.abc import Callable

import devtools.ya.core.config
import devtools.ya.core.error
import devtools.ya.core.report
import exts.archive
import exts.os2
import exts.process
import exts.shlex2
import exts.timer
import exts.windows
import devtools.ya.test.const
import devtools.ya.yalibrary.runner.schedule_strategy as schedule_strategy

from devtools.ya.build.graph_description import GraphNode, GraphNodeUid, SelfUid, StaticUid
from yalibrary import status_view
from devtools.ya.yalibrary.active_state import Cancelled
from yalibrary.fetcher.resource_fetcher import fetch_resource_if_need
from yalibrary.runner import build_root
from yalibrary.runner import patterns as ptn
from yalibrary.runner import runqueue
from yalibrary.runner import statcalc
from yalibrary.runner import worker_threads
from yalibrary.runner import task_cache
from yalibrary.runner.command_file.python import command_file as cf
from yalibrary.runner.tasks.enums import WorkerPoolType
from yalibrary.status_view.helpers import format_paths
from yalibrary.store.usage_map import UsageMap

import yalibrary.runner.sandboxing as sandboxing


logger = logging.getLogger(__name__)


if tp.TYPE_CHECKING:
    from build.ya_make import Context
    from yalibrary.runner.tasks.pattern import PreparePattern
    from yalibrary.runner.tasks.resource import PrepareResource
    from yalibrary.runner.tasks.prepare import PrepareNodeTask
    from yalibrary.runner.tasks.dist_cache import RestoreFromDistCacheTask, PutInDistCacheTask
    from yalibrary.runner.tasks.cache import RestoreFromCacheTask, PutInCacheTask, WriteThroughCachesTask
    from yalibrary.runner.tasks.result import ResultNodeTask, ResultArtifacts
    from yalibrary.runner.tasks.run import RunNodeTask
    from yalibrary.runner.tasks.distbuild import DistDownloadTask

ContentUid = tp.NewType('ContentUid', str)  # TODO Move to more appropriate place later
type ExecutionLog = dict[GraphNodeUid, dict]
type BuildErrors = dict[GraphNodeUid, str]
type ExitCode = int
type OutputReplacements = list[tuple[str, str]]
type RunResult = tuple["ResultArtifacts", BuildErrors, ExitCode, ExecutionLog, dict[GraphNodeUid, ExitCode]]
type Task = tp.Any  # TODO Create real base class for all the tasks and use it here


class EarlyStoppingException(Cancelled):
    pass


def run(
    ctx: "Context",
    app_ctx: types.ModuleType,
    callback: Callable,
    output_replacements: OutputReplacements | None = None,
) -> RunResult:
    try:
        with contextlib.ExitStack() as exit_stack:
            ctx.task_context = TaskContext(exit_stack, ctx, app_ctx, callback, output_replacements)
            return ctx.task_context.run()
    except Exception as e:
        logger.debug("Local executor failed with exception %s", str(e))
        ctx.local_runner_ready.set(False)
        raise


class TaskContext(object):
    def __init__(
        self,
        exit_stack: contextlib.ExitStack,
        ctx: "Context",
        app_ctx: types.ModuleType,
        callback: Callable,
        output_replacements: OutputReplacements | None,
    ) -> None:
        self._exit_stack = exit_stack
        self._ctx = weakref.proxy(ctx)
        self._app_ctx = app_ctx
        self._callback = callback
        self._cache = ctx.cache
        self._dist_cache = ctx.dist_cache
        self._threads = ctx.threads
        self._continue_on_fail: bool = ctx.graph['conf'].get('keepon', False)
        self._need_symlinks = ctx.create_symlinks
        self._suppress_outputs_conf = ctx.suppress_outputs_conf
        self._need_output = self._ctx.create_output
        self._resources_map: dict[str, dict] = {x['pattern']: x for x in ctx.graph['conf']['resources']}

        self.opts = ctx.opts
        self.state = app_ctx.state.sub(__name__)
        self.resources: dict[str, dict] = {}
        self.fetchers_storage = app_ctx.fetchers_storage
        self.content_uids: bool = self.opts.force_content_uids
        logger.debug("content UIDs %s in runner", "*enabled*" if self.content_uids else "*disabled*")

        self._res: "ResultArtifacts" = collections.defaultdict(list)
        self._execution_log: ExecutionLog = {}
        self._build_errors: BuildErrors = {}

        # Process can hit soft limit of RLIMIT_NOFILE
        _setup_ulimit()

        wait_local_executor_init_fn = self._init_local_executor()

        self._ienv = sandboxing.FuseSandboxing(ctx.opts, ctx.src_dir)
        self._exit_stack.callback(self._ienv.stop)

        self._init_workers()
        self._init_build_root_set()

        timer = exts.timer.Timer(__name__)
        self._init_patterns()
        timer.show_step('resolve')

        self._init_nodes()
        timer.show_step('build nodes')

        self._init_runq_and_caches(output_replacements)
        self._init_essential_tasks()
        self._init_prepare_task()
        timer.show_step('build tasks')

        if wait_local_executor_init_fn:
            wait_local_executor_init_fn()
            timer.show_step('wait local executor')

    def fast_fail(self, fatal: bool = False) -> None:
        if fatal or not self._continue_on_fail:

            def stopping():
                raise EarlyStoppingException()

            self.state.stopping(stopping)

    def exec_run_node(self, node: "Node", parent_task: tp.Any) -> None:
        import yalibrary.runner.tasks.resource

        deps = (
            [self.task_cache(x, self.prepare_node) for x in node.dep_nodes()]
            + [self.pattern_cache(x) for x in node.unresolved_patterns()]
            + [
                self.resource_cache(
                    tuple(sorted(x.items())),
                    deps=yalibrary.runner.tasks.resource.PrepareResource.dep_resources(self, x),
                )
                for x in node.resources
            ]
        )

        self.runq.add(self.run_node(node), joint=parent_task, deps=deps)

    def prepare_pattern(self, pattern: str) -> "PreparePattern":
        import yalibrary.runner.tasks.pattern

        # XXX: remove transient_resource_dir needed for $(VCS) pattern only.
        return yalibrary.runner.tasks.pattern.PreparePattern(
            pattern,
            self,
            self._ctx.res_dir,
            self._transient_resource_dir,
            self._resources_map,
            self.fetchers_storage,
            fetch_resource_if_need,
            self._execution_log,
        )

    def prepare_resource(self, uri_description: dict) -> "PrepareResource":
        import yalibrary.runner.tasks.resource

        return yalibrary.runner.tasks.resource.PrepareResource(
            uri_description,
            self,
            self._transient_resource_dir,
            self.fetchers_storage,
            fetch_resource_if_need,
            self._execution_log,
            self._ctx.cache,
        )

    def prepare_node(self, node: "Node") -> "PrepareNodeTask":
        import yalibrary.runner.tasks.prepare

        return yalibrary.runner.tasks.prepare.PrepareNodeTask(node, self, self._ctx.cache, self._dist_cache)

    def restore_from_dist_cache(self, node: "Node") -> "RestoreFromDistCacheTask":
        import yalibrary.runner.tasks.dist_cache

        return yalibrary.runner.tasks.dist_cache.RestoreFromDistCacheTask(
            node, self._build_root_set, self, self._dist_cache, self._execution_log, self.save_links_regex
        )

    def put_in_dist_cache(self, node: "Node", build_root: build_root.BuildRoot) -> "PutInDistCacheTask":
        import yalibrary.runner.tasks.dist_cache

        return yalibrary.runner.tasks.dist_cache.PutInDistCacheTask(
            node, build_root, self._dist_cache, self.opts.yt_store_codec, self._execution_log
        )

    def restore_from_cache(self, node: "Node") -> "RestoreFromCacheTask":
        import yalibrary.runner.tasks.cache

        return yalibrary.runner.tasks.cache.RestoreFromCacheTask(
            node,
            self._build_root_set.new(node.outputs, node.refcount, node.dir_outputs, compute_hash=node.hashable),
            self,
            self._ctx.cache,
            self._dist_cache,
            self._execution_log,
        )

    def put_in_cache(self, node: "Node", build_root: build_root.BuildRoot) -> "PutInCacheTask":
        import yalibrary.runner.tasks.cache

        return yalibrary.runner.tasks.cache.PutInCacheTask(
            node,
            build_root,
            self._ctx.cache,
            self.opts.cache_codec,
            self._execution_log,
            dir_outputs_test_mode=self.opts.dir_outputs_test_mode,
        )

    def write_through_caches(self, node: "Node", build_root: build_root.BuildRoot) -> "WriteThroughCachesTask":
        import yalibrary.runner.tasks.cache

        return yalibrary.runner.tasks.cache.WriteThroughCachesTask(
            node, self, self._ctx.cache, self._dist_cache, build_root
        )

    def clear_uid(self, uid: GraphNodeUid):
        self._ctx.cache.clear_uid(uid)

    def result_node(self, node: "Node", provider: Task | None = None) -> "ResultNodeTask":
        import yalibrary.runner.tasks.result

        return yalibrary.runner.tasks.result.ResultNodeTask(
            node,
            self,
            self._callback,
            self._ctx.create_output,
            self._ctx.output_result,
            self._ctx.create_symlinks,
            self._ctx.symlink_result,
            self._ctx.suppress_outputs_conf,
            self._ctx.install_result,
            self._ctx.bin_result,
            self._ctx.lib_result,
            self._res,
            self._ctx.cache_test_statuses,
            provider=provider,
        )

    def eager_result(self, provider: Task):
        if not self.opts.eager_execution:
            return

        node = provider._node
        if node.uid in self.results:
            self.runq.add(
                self.result_node(node, provider),
                deps=[self.clean_symres_task],
                inplace_execution=self.opts.eager_execution,
            )

    def run_node(self, node: "Node") -> "RunNodeTask":
        import yalibrary.runner.tasks.run

        return yalibrary.runner.tasks.run.RunNodeTask(
            node,
            self._build_root_set.new(node.outputs, node.refcount, node.dir_outputs, compute_hash=node.hashable),
            self,
            self._threads,
            self._test_threads,
            self._execution_log,
            self._build_errors,
            self._app_ctx.display,
            None,
            self._callback,
            self._ctx.cache,
            self._dist_cache,
            self._ienv.manager(),
        )

    def run_dist_node(self, node: "Node") -> "DistDownloadTask":
        import yalibrary.runner.tasks.distbuild

        return yalibrary.runner.tasks.distbuild.DistDownloadTask(
            node,
            self,
            self._build_root_set,
            self.patterns,
            self.save_links_regex,
            self._callback,
            self._dist_cache,
            self.opts.mds_read_account,
            self._execution_log,
            dump_evlog_stat=self.opts.evlog_dump_node_stat,
            store_links_in_memory=self.opts.store_links_in_memory,
            use_universal_fetcher=self.opts.use_universal_fetcher_for_dist_results,
        )

    def dispatch_uid(self, uid: GraphNodeUid, *args, **kwargs) -> None:
        node = self.nodes[uid]
        try:
            task = self.task_cache(node)
            self.runq.dispatch(task, *args, **kwargs)
        except KeyError:
            pass

    def dispatch_all(self, *args, **kwargs) -> None:
        self.runq.dispatch_all(*args, **kwargs)

    def signal_ready(self) -> None:
        # signal distbuild runner that execution graph is ready
        # should dominate all non-exceptional 'returns'
        logger.debug("Local executor is ready")
        self._ctx.local_runner_ready.set(True)

    def run(self) -> RunResult:
        TRUNCATE_STDERR = 2 * 80  # Two lines from begin and end

        self._ienv.start()
        start_time = time.time()
        try:
            self.runq.add(self.clean_symres_task)
            self.runq.add(self.prepare_all_nodes_task)
            self.runq.add(self.compact_cache_task, deps=[self.prepare_all_nodes_task])

            if self.opts.use_distbuild:
                from yalibrary.runner.tasks import distbuild

                wrapped_process_queue = distbuild.wrap_sending_telemetry_for(self._process_queue)
                wrapped_process_queue()
            else:
                self._process_queue()

            self.runq.add(self.clean_build_task, deps=[])
            self._process_queue()

            self.runq.dispatch_all()
        except EarlyStoppingException:
            pass
        finally:
            self._exit_stack.pop_all().close()

            if hasattr(self._ctx.cache, 'stats'):
                self._ctx.cache.stats(self._execution_log)

            if self._dist_cache and hasattr(self._dist_cache, 'stats'):
                dist_cache_evlog_writer = (
                    self._app_ctx.evlog.get_writer('yt_store') if getattr(self._app_ctx, 'evlog', None) else None
                )
                self._dist_cache.stats(self._execution_log, dist_cache_evlog_writer)

        wall_time = time.time() - start_time

        if not self.opts.use_distbuild:
            for uid in self.nodes.keys():
                if uid not in self._execution_log:
                    self._execution_log[uid] = {'cached': True}

        replay_info = list(self.runq.replay())

        exit_code_map: dict[GraphNodeUid, ExitCode] = {}
        for group in replay_info:
            for task_info in group:
                try:
                    rc = task_info.task.exit_code
                except AttributeError:
                    continue
                stderr = getattr(task_info.task, "raw_stderr", None)
                if rc:
                    logger.debug(
                        "Task %s failed with %s exit code: %s",
                        task_info.task,
                        rc,
                        '\n' + stderr if stderr else 'no strderr was provided',
                    )
                    task_uid = getattr(task_info.task, 'uid', getattr(task_info.task, 'short_name', 'UnknownTask'))
                    exit_code_map[task_uid] = rc
                elif stderr:
                    if len(stderr) <= TRUNCATE_STDERR * 2:
                        truncated_stderr = stderr
                    else:
                        truncated_stderr = "\n".join((stderr[:TRUNCATE_STDERR], "...", stderr[-TRUNCATE_STDERR:]))
                    logger.debug("Task %s has stderr:\n%s", task_info.task, truncated_stderr)

        merged_exit_code = devtools.ya.core.error.merge_exit_codes([0] + list(exit_code_map.values()))
        logger.debug("Merged exit code: %d", merged_exit_code)

        critical_path: list[dict] = []
        for task_info in statcalc.calc_critical(replay_info):
            critical_path.append(
                {
                    'name': str(task_info.task),  # TODO: better
                    'timing': task_info.timing,
                    'type': task_info.task.short_name(),
                }
            )

        data = statcalc.calc_stat(replay_info)
        data.update(
            {
                'critical_path': critical_path,
                'wall_time': wall_time,
                'build_type': self.opts.build_type,
                'flags': self.opts.flags,
                'rel_targets': self.opts.rel_targets,
                'threads': self.opts.build_threads,
            }
        )

        logger.debug('Profile of graph execution %s', json.dumps(data))
        devtools.ya.core.report.telemetry.report(devtools.ya.core.report.ReportTypes.PROFILE_BY_TYPE, data)

        if self._dist_cache:
            logger.debug('Average compression ratio: %0.2f', self._dist_cache.avg_compression_ratio)

        # Drop empty entries (with fake/suppressed artifacts)
        res: "ResultArtifacts" = {k: [x for x in v if x] for k, v in self._res.items()}

        return res, self._build_errors, merged_exit_code, self._execution_log, exit_code_map

    def _init_local_executor(self) -> Callable | None:
        if self.opts.use_distbuild:
            return

        from yalibrary.runner.tasks.run import LocalExecutor, PopenExecutor

        if self.opts.local_executor:
            self.executor_type = LocalExecutor
            if self.opts.executor_address:
                self.executor_address = self.opts.executor_address
                return
            # local executor is launched in a separate process
            # it takes some time till it starts being available
            # so we launch it early and check the status later
            from devtools.executor.python.executor import start_executor

            # Don't cache_stderr to avoid belated reading of special tags
            _, address, wait_fn = start_executor(
                cache_stderr=False, debug=devtools.ya.core.config.is_test_mode(), wait_init=False
            )
            self.executor_address = address
            return wait_fn
        else:
            self.executor_type = PopenExecutor
            self.executor_address = None
            return

    def _init_workers(self):
        self.build_time_cache: UsageMap | None = None
        if not self.opts.use_distbuild:
            build_time_cache_availability = schedule_strategy.BuildTimeCacheAvailability.YES
            try:
                cache_size = 20 << 20  # 20MB
                self.build_time_cache = UsageMap(
                    os.path.join(self._ctx.garbage_dir, 'cache', 'runner_build_time', 'rbt'), file_size_b=cache_size
                )
                self._exit_stack.enter_context(contextlib.closing(self.build_time_cache))
            except Exception as e:
                build_time_cache_availability = schedule_strategy.BuildTimeCacheAvailability.NO
                logger.warning('Could not create build time cache due to error {!r}'.format(e))
        else:
            build_time_cache_availability = schedule_strategy.BuildTimeCacheAvailability.NEVER

        self._test_threads = self.opts.test_threads or self._threads
        net_threads = self.opts.yt_store_threads + self.opts.dist_store_threads
        io_limit = min(self.opts.link_threads, self._threads)

        cap = worker_threads.ResInfo(
            io=io_limit,
            cpu=self._threads,
            test=self._test_threads,
            download=self._threads + net_threads,
            upload=net_threads,
        )
        worker_pools = {WorkerPoolType.BASE: self._threads, WorkerPoolType.SERVICE: net_threads + 1}
        strategy = schedule_strategy.Strategies.pick(self.opts.schedule_strategy, build_time_cache_availability)

        self._workers = worker_threads.WorkerThreads(
            state=self.state,
            worker_pools=worker_pools,
            zero=worker_threads.ResInfo(),
            cap=cap,
            evlog=getattr(self._app_ctx, 'evlog', None),
            schedule_strategy=strategy,
        )

        self._exit_stack.callback(self._workers.join)
        # Workers expect stopped state
        self._exit_stack.callback(self.state.stop)

    def _init_build_root_set(self):
        self._build_root_set = self._exit_stack.enter_context(
            build_root.BuildRootSet(
                self.opts.bld_root,
                self.opts.keep_temps,
                self._workers.add,
                self.opts.limit_build_root_size,
                validate_content=self.opts.validate_build_root_content,
            )
        )
        os.environ['LC_ALL'] = 'C'
        os.environ['LANG'] = 'en'
        os.environ['WINEPREFIX'] = self._build_root_set.new([], 0).create()
        self._transient_resource_dir = self._build_root_set.new([], 0).create()

    def _init_patterns(self):
        patterns = ptn.Patterns()
        patterns['SOURCE_ROOT'] = exts.windows.win_path_fix(self._ctx.src_dir)
        patterns['TOOL_ROOT'] = exts.windows.win_path_fix(self._ctx.res_dir)
        patterns['RESOURCE_ROOT'] = exts.windows.win_path_fix(self._transient_resource_dir)

        if self.opts.oauth_token_path or self.opts.sandbox_oauth_token_path:
            token_path = self.opts.oauth_token_path or self.opts.sandbox_oauth_token_path
            patterns['YA_TOKEN_PATH'] = exts.windows.win_path_fix(token_path)
        elif (self.opts.oauth_token or self.opts.sandbox_oauth_token) and self.opts.store_oauth_token:
            token = self.opts.oauth_token or self.opts.sandbox_oauth_token
            patterns['YA_TOKEN_PATH'] = exts.windows.win_path_fix(
                self._exit_stack.enter_context(self._token_context(token))
            )
        if self.opts.frepkage_root:
            patterns['FREPKAGE_ROOT'] = exts.windows.win_path_fix(self.opts.frepkage_root)
        self.patterns = patterns

    def _init_nodes(self):
        self.results: frozenset[GraphNodeUid] = frozenset(self._ctx.graph['result'])
        self.nodes: dict[GraphNodeUid, "Node"] = {
            x.get('uid'): Node(x, self, x.get('uid') in self.results) for x in self._ctx.graph['graph']
        }

        if not self.opts.use_distbuild:
            for n in self.nodes.values():
                for d in n.consume():
                    self.nodes[d].refcount += 1

        # Do not delete results for last_failed module
        for suite in self._ctx.tests:
            for tuid in suite.result_uids:
                if tuid in self.nodes:
                    self.nodes[tuid].refcount += 1

    def _init_runq_and_caches(self, output_replacements: OutputReplacements | None):
        queue_status = status_view.Status()
        term_view = status_view.TermView(
            queue_status,
            self._app_ctx.display,
            self.opts.output_style == 'ninja',
            self.opts.ext_progress,
            False,
            output_replacements=output_replacements,
            use_roman_numerals=self.opts.use_roman_numerals,
        )
        self._ticker = status_view.TickThrottle(term_view.tick, 0.1)
        self._exit_stack.callback(lambda: self._ticker.tick(force=True))
        self._exit_stack.callback(term_view.tick)

        self.runq = runqueue.RunQueue(self._workers.add, queue_status.listener())
        self.task_cache = task_cache.TaskCache(self.runq)
        self.pattern_cache = task_cache.TaskCache(self.runq, self.prepare_pattern)
        self.resource_cache = task_cache.TaskCache(self.runq, self.prepare_resource)

    def _init_essential_tasks(self):
        import yalibrary.runner.tasks.cache

        self.compact_cache_task = yalibrary.runner.tasks.cache.CompactCacheTask(
            self._ctx.cache, self.state, self.opts, self._execution_log
        )
        self.clean_symres_task = yalibrary.runner.tasks.cache.CleanSymresTask(
            self._ctx.symlink_result, self.state, self.opts, self._execution_log
        )
        self._exit_stack.callback(self._release_symres_lock)
        self.clean_build_task = yalibrary.runner.tasks.cache.CleanBuildRootTask(
            self._build_root_set, self.state, self.opts, self._execution_log
        )

    def _init_prepare_task(self):
        import yalibrary.runner.tasks.prepare

        save_links_for: list[str] = getattr(self.opts, 'save_links_for', [])

        if self.opts.use_distbuild:
            res_nodes = [x for x in self.nodes.values() if x.is_result_node]
            if self.opts.output_only_tests:  # XXX
                # We need to download test's output (testing_output_stuff.tar, etc) from distbuild - use output_uids instead of result_uids
                res_set = set(sum([t.output_uids for t in self._ctx.tests], []))
                res_nodes = [x for x in res_nodes if x.uid in res_set]
            download_test_results = _is_test_requested(self.opts)
            if not self.opts.download_artifacts and download_test_results:
                save_links_for += _get_link_outputs(res_nodes)
                save_links_for = list(set(save_links_for))

            self.results: frozenset[GraphNodeUid] = frozenset([n.uid for n in res_nodes])
            self.prepare_all_nodes_task = yalibrary.runner.tasks.prepare.PrepareAllDistNodesTask(
                self, self.opts.download_artifacts or download_test_results, res_nodes
            )
        else:
            self.prepare_all_nodes_task = yalibrary.runner.tasks.prepare.PrepareAllNodesTask(
                self.nodes.values(), self, self._ctx.cache, self._dist_cache
            )

        self.save_links_regex = None
        if save_links_for:
            self.save_links_regex = re.compile("|".join(fnmatch.translate(e) for e in save_links_for))

    @contextlib.contextmanager
    def _token_context(self, token: str):
        token_file = os.path.join(self._transient_resource_dir, '.ya_token')
        with open(token_file, 'w') as f:
            f.write(token)
        try:
            yield token_file
        finally:
            try:
                os.remove(token_file)
            except OSError:
                # May have removed during build root cleanup
                pass

    def _release_symres_lock(self):
        if lock := self.clean_symres_task.get_shared_lock():
            lock.release()

    def _process_queue(self):
        while True:
            try:
                f = self._workers.next(
                    timeout=self.opts.status_refresh_interval, ready_to_stop=lambda: self.runq.pending == 0
                )
            except StopIteration:
                break
            self._ticker.tick(False, self.compact_cache_task.fmt_size())
            if f:
                f()


class Node:
    __slots__ = (
        '_task_context',
        'args',
        'is_result_node',
        'uid',
        'max_dist',
        'refcount',
        'content_uid',
        'output_digests',
        'custom_commands',
        '__dep_nodes',
    )

    def __init__(self, kwargs: GraphNode, task_context: TaskContext, is_result_node: bool = False):
        self.args = kwargs
        self.is_result_node = is_result_node
        self._task_context = weakref.proxy(task_context)
        self.uid: GraphNodeUid = kwargs.get('uid')
        self.max_dist = 0
        self.refcount = 0
        self.content_uid: ContentUid = None
        self.output_digests: build_root.OutputDigests | None = None
        self.custom_commands: Callable | None = None
        self.__dep_nodes: list[tp.Self] | None = None

    @property
    def self_uid(self) -> SelfUid | None:
        return self.args.get('self_uid', None)

    @property
    def static_uid(self) -> StaticUid | None:
        return self.args.get('static_uid', None)

    @property
    def hashable(self) -> bool:
        return True if self.self_uid else False

    @property
    def inputs(self) -> list[str] | None:
        return self.args.get('inputs')

    @property
    def outputs(self) -> list[str] | None:
        return self.args.get('outputs')

    @property
    def resources(self):
        return self.args.get('resources', [])

    @property
    def tared_outputs(self) -> list[str] | None:
        return self.args.get('tared_outputs', [])

    @property
    def dir_outputs(self) -> list[str] | None:
        return self.args.get('dir_outputs', [])

    @property
    def tags(self):
        return self.args.get('tags', [])

    @property
    def deps(self):
        return self.args.get('deps')

    @property
    def kv(self):
        return self.args.get('kv')

    @property
    def requirements(self):
        return self.args.get('requirements', {})

    @property
    def cacheable(self):
        return self.args.get('cache', True)

    @property
    def priority(self):
        return self.args.get('priority')

    @property
    def target_properties(self):
        return self.args.get('target_properties', {})

    @property
    def ignore_broken_dependencies(self):
        return self.args.get('ignore_broken_dependencies', False)

    @property
    def stable_dir_outputs(self):
        return self.args.get('stable_dir_outputs', False)

    @property
    def has_self_uid_support(self):
        return self.self_uid is not None and self.cacheable

    def _commands(self):
        def command(cmd, defaults={}):
            keys = 'cmd_args', 'cwd', 'env', 'stdout', 'stderr'
            return {key: cmd.get(key) or defaults.get(key) for key in keys}

        if self.args.get('cmd_args'):
            yield command(self.args)

        for cmd in self.args.get('cmds', ()):
            yield command(cmd, defaults=self.args)

    def command_args(self, build_root, patterns_sub):
        args = patterns_sub.fill(self._commands())
        args = cf.CommandArgsPacker(build_root).apply(args, 'cmd_args')
        return args

    def commands(self, build_root: str):
        if self.custom_commands:
            return self.custom_commands(build_root)
        p = self._task_context.patterns.sub()
        p['BUILD_ROOT'] = exts.windows.win_path_fix(build_root)
        return self.command_args(build_root, p)

    def __str__(self):
        lim = 6
        if len(self.outputs) < lim:
            outputs = self.outputs
        else:
            outputs = self.outputs[:lim] + ["<{} more outputs>".format(len(self.outputs) - lim)]
        return self.uid + ' '.join(outputs)

    def format(self):
        return format_paths(self.inputs, self.outputs, self.kv)

    def provide(self):
        return [self.uid]

    def consume(self):
        return self.deps

    def unresolved_patterns(self):
        return set(self._task_context.patterns.unresolved([self._commands(), self.inputs])) - {'BUILD_ROOT'}

    def dep_nodes(self):
        if self.__dep_nodes is None:
            self.__dep_nodes = [self._task_context.nodes[d] for d in self.consume()]
        return self.__dep_nodes

    def fmt(self, tags: list[str] | None = None):
        from yalibrary.status_view.helpers import fmt_node

        return fmt_node(self.inputs, self.outputs, self.kv, tags)


def _setup_ulimit():
    try:
        import resource
    except ImportError:
        return

    limits = (
        (resource.RLIMIT_NOFILE, "RLIMIT_NOFILE"),
        # Rationale: https://st.yandex-team.ru/DEVTOOLSSUPPORT-24994#6391a4768b1f367b3fed2396
        # TODO: Set an appropriate soft limit if hard = unlimited
        # (resource.RLIMIT_STACK, "RLIMIT_STACK"),
    )

    for res, descr in limits:
        soft, hard = resource.getrlimit(res)
        logger.debug("%s limits: (%s, %s)", descr, soft, hard)
        try:
            resource.setrlimit(res, (hard, hard))
        except Exception as e:
            logger.debug("Failed to set %s limit: %s", descr, e)


def _get_link_outputs(res_nodes: list["Node"]):
    link_outputs: set[str] = set()
    for n in res_nodes:
        for o in n.outputs:
            if not o.endswith(devtools.ya.test.const.TRACE_FILE_NAME):
                link_outputs.add(os.path.basename(o))
    return list(link_outputs)


def _is_test_requested(opts):
    return bool(opts.run_tests) or _run_tests_set_in_target_platforms(opts)


def _run_tests_set_in_target_platforms(opts):
    for platform in getattr(opts, "target_platforms", []):
        if platform.get("run_tests", False):
            return True

    return False
