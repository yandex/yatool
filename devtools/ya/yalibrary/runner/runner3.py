import collections
import contextlib2
import exts.yjson as json
import fnmatch
import logging
import os
import re
import time

import typing as tp  # noqa

import six

import core.config
import core.error
import core.report
import exts.archive
import exts.os2
import exts.process
import exts.shlex2
import exts.timer
import exts.windows
import devtools.ya.test.const
import devtools.ya.yalibrary.runner.schedule_strategy as schedule_strategy

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


class EarlyStoppingException(Cancelled):
    pass


def fmt_node(node, tags=None, status=None):
    from yalibrary.status_view.helpers import fmt_node as _fmt_node

    return _fmt_node(node.inputs, node.outputs, node.kv, tags, status)


def run(ctx, app_ctx, callback, output_replacements=None):
    try:
        with contextlib2.ExitStack() as exit_stack:
            return _run(ctx, app_ctx, callback, exit_stack, output_replacements=output_replacements)
    except Exception as e:
        logger.debug("Local executor failed with exception %s", str(e))
        ctx.local_runner_ready.set(False)
        raise


def setup_ulimit():
    try:
        import resource
    except ImportError:
        return

    def setup(res, descr):
        soft, hard = resource.getrlimit(res)
        logger.debug("%s limits: (%s, %s)", descr, soft, hard)
        try:
            resource.setrlimit(res, (hard, hard))
        except Exception as e:
            logger.debug("Failed to set %s limit: %s", descr, e)

    setup(resource.RLIMIT_NOFILE, "RLIMIT_NOFILE")
    # Rationale: https://st.yandex-team.ru/DEVTOOLSSUPPORT-24994#6391a4768b1f367b3fed2396
    # TODO: Set an appropriate soft limit if hard = unlimited
    # setup(resource.RLIMIT_STACK, "RLIMIT_STACK")


def get_link_outputs(res_nodes):
    link_outputs = set()
    for n in res_nodes:
        for o in n.outputs:
            if not o.endswith(devtools.ya.test.const.TRACE_FILE_NAME):
                link_outputs.add(os.path.basename(o))
    return list(link_outputs)


def _run(ctx, app_ctx, callback, exit_stack, output_replacements=None):
    # type: (tp.Any, tp.Any, tp.Any, contextlib2.ExitStack, tp.Any) -> tp.Tuple[dict[tp.Any, list[tp.Any]], dict[tp.Any, tp.Any], None | int, dict[tp.Any, tp.Any], dict[tp.Any, tp.Any]]
    opts = ctx.opts
    cache = ctx.cache
    dist_cache = ctx.dist_cache
    graph = ctx.graph
    threads = ctx.threads
    close_fds = not exts.windows.on_win()
    need_symlinks = ctx.create_symlinks
    suppress_outputs_conf = ctx.suppress_outputs_conf
    need_output = ctx.create_output

    output_result = ctx.output_result
    symlink_result = ctx.symlink_result
    install_result = ctx.install_result
    bin_result = ctx.bin_result
    lib_result = ctx.lib_result

    fetchers_storage = app_ctx.fetchers_storage
    display = app_ctx.display

    state = app_ctx.state.sub(__name__)

    TRUNCATE_STDERR = 2 * 80  # Two lines from begin and end

    # Process can hit soft limit of RLIMIT_NOFILE
    setup_ulimit()

    create_local_executor = not opts.use_distbuild and opts.local_executor and not opts.executor_address
    wait_local_executor_init_fn = local_executor_address = None
    if create_local_executor:
        # local executor is launched in a separate process
        # it takes some time till it starts being available
        # so we launch it early and check the status later
        from devtools.executor.python import executor

        # Don't cache_stderr to avoid belated reading of special tags
        _, local_executor_address, wait_local_executor_init_fn = executor.start_executor(
            cache_stderr=False, debug=core.config.is_test_mode(), wait_init=False
        )

    build_time_cache = None
    if not opts.use_distbuild:
        build_time_cache_availability = schedule_strategy.BuildTimeCacheAvailability.YES
        try:
            build_time_cache = UsageMap(os.path.join(ctx.garbage_dir, 'cache', 'runner_build_time', 'rbt'))
        except Exception as e:
            build_time_cache_availability = schedule_strategy.BuildTimeCacheAvailability.NO
            logger.warning('Could not create build time cache due to error {!r}'.format(e))
    else:
        build_time_cache_availability = schedule_strategy.BuildTimeCacheAvailability.NEVER

    ienv = sandboxing.FuseSandboxing(opts, ctx.src_dir)

    class IEnvContext(object):
        def __enter__(self):
            return ienv

        def __exit__(self, *exc_details):
            ienv.stop()
            return False

    ienv = exit_stack.enter_context(IEnvContext())

    test_threads = ctx.opts.test_threads or threads
    net_threads = ctx.opts.yt_store_threads + ctx.opts.dist_store_threads
    io_limit = min(ctx.opts.link_threads, threads)

    cap = worker_threads.ResInfo(
        io=io_limit, cpu=threads, test=test_threads, download=threads + net_threads, upload=net_threads
    )
    worker_pools = {WorkerPoolType.BASE: threads, WorkerPoolType.SERVICE: net_threads + 1}
    strategy = schedule_strategy.Strategies.pick(opts.schedule_strategy, build_time_cache_availability)

    workers = worker_threads.WorkerThreads(
        state=state,
        worker_pools=worker_pools,
        zero=worker_threads.ResInfo(),
        cap=cap,
        evlog=getattr(app_ctx, 'evlog', None),
        schedule_strategy=strategy,
    )

    class WorkersContext(object):
        def __enter__(self):
            return workers

        def __exit__(self, *exc_details):
            workers.join()
            return False

    class StateContext(object):
        def __enter__(self):
            return state

        def __exit__(self, *exc_details):
            state.stop()
            return False

    workers = exit_stack.enter_context(WorkersContext())
    # Workers expect stopped state
    state = exit_stack.enter_context(StateContext())

    build_root_set = exit_stack.enter_context(
        build_root.BuildRootSet(
            ctx.opts.bld_root,
            ctx.opts.keep_temps,
            workers.add,
            ctx.opts.limit_build_root_size,
            validate_content=ctx.opts.validate_build_root_content,
        )
    )
    continue_on_fail = graph['conf'].get('keepon', False)
    os.environ['LC_ALL'] = 'C'
    os.environ['LANG'] = 'en'
    os.environ['WINEPREFIX'] = build_root_set.new([], 0).create()
    transient_resource_dir = build_root_set.new([], 0).create()

    timer = exts.timer.Timer(__name__)
    conf = graph['conf']
    resources_map = {x['pattern']: x for x in conf['resources']}
    patterns = ptn.Patterns()
    patterns['SOURCE_ROOT'] = exts.windows.win_path_fix(ctx.src_dir)
    patterns['TOOL_ROOT'] = exts.windows.win_path_fix(ctx.res_dir)
    patterns['TESTS_DATA_ROOT'] = exts.windows.win_path_fix(
        ctx.opts.arcadia_tests_data_path or os.path.normpath(os.path.join(ctx.src_dir, '..', 'arcadia_tests_data'))
    )
    patterns['RESOURCE_ROOT'] = exts.windows.win_path_fix(transient_resource_dir)

    if ctx.opts.oauth_token_path:
        patterns['YA_TOKEN_PATH'] = exts.windows.win_path_fix(ctx.opts.oauth_token_path)
    elif ctx.opts.oauth_token and ctx.opts.store_oauth_token:

        class TokenContext(object):
            def __init__(self, transient_resource_dir):
                self._token_file = os.path.join(transient_resource_dir, '.ya_token')

            def __enter__(self):
                with open(self._token_file, 'w') as f:
                    f.write(ctx.opts.oauth_token)
                return self._token_file

            def __exit__(self, *exc_details):
                try:
                    os.remove(self._token_file)
                except Exception:
                    # May have removed during build root cleanup.
                    pass
                return False

        patterns['YA_TOKEN_PATH'] = exts.windows.win_path_fix(
            exit_stack.enter_context(TokenContext(transient_resource_dir))
        )

    if ctx.opts.frepkage_root:
        patterns['FREPKAGE_ROOT'] = exts.windows.win_path_fix(ctx.opts.frepkage_root)

    resources = dict()

    timer.show_step('resolve')

    ninja = opts.output_style == 'ninja'

    class Noda:
        __slots__ = (
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

        def __init__(self, kwargs, is_result_node=False):
            self.args = kwargs
            self.is_result_node = is_result_node
            self.uid = kwargs.get('uid')
            self.max_dist = 0
            self.refcount = 0
            self.content_uid = None
            self.output_digests = None
            self.custom_commands = None
            self.__dep_nodes = None

        @property
        def self_uid(self):
            return self.args.get('self_uid', None)

        @property
        def static_uid(self):
            return self.args.get('static_uid', None)

        @property
        def hashable(self):
            return True if self.self_uid else False

        @property
        def inputs(self):
            return self.args.get('inputs')

        @property
        def outputs(self):
            return self.args.get('outputs')

        @property
        def resources(self):
            return self.args.get('resources', [])

        @property
        def tared_outputs(self):
            return self.args.get('tared_outputs', [])

        @property
        def dir_outputs(self):
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

        def _unresolved_sources(self):
            yield self._commands()
            yield self.inputs

        def _command_args(self, build_root, patterns_sub):
            args = patterns_sub.fill(self._commands())
            args = cf.CommandArgsPacker(build_root).apply(args, 'cmd_args')
            return args

        def commands(self, build_root):
            if self.custom_commands:
                return self.custom_commands(build_root)
            p = patterns.sub()
            p['BUILD_ROOT'] = exts.windows.win_path_fix(build_root)
            return self._command_args(build_root, p)

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
            return set(patterns.unresolved(self._unresolved_sources())) - {'BUILD_ROOT'}

        def dep_nodes(self):
            if self.__dep_nodes is None:
                self.__dep_nodes = [who_provides[d] for d in self.consume()]
            return self.__dep_nodes

    results = frozenset(graph['result'])

    nodes = [Noda(x, x.get('uid') in results) for x in graph['graph']]

    timer.show_step('build nodes')

    who_provides = dict()
    refs = dict()

    for n in nodes:
        for p in n.provide():
            refs[p] = 0
            who_provides[p] = n
    timer.show_step('build who provides, ref count')

    def setup_incremental_cleanup():
        if not ctx.opts.use_distbuild:
            for n in nodes:
                for d in n.consume():
                    refs[d] += 1

            for u, v in list(refs.items()):
                who_provides[u].refcount = v

        # Do not delete results for last_failed module
        for suite in ctx.tests:
            for tuid in suite.result_uids:
                if tuid in who_provides:
                    who_provides[tuid].refcount += 1

    setup_incremental_cleanup()

    timer.show_step('build ref count')

    queue_status = status_view.Status()
    term_view = status_view.TermView(
        queue_status,
        display,
        ninja,
        opts.ext_progress,
        False,
        output_replacements=output_replacements,
        use_roman_numerals=opts.use_roman_numerals,
    )
    ticker = status_view.TickThrottle(term_view.tick, 0.1)

    class TermContext(object):
        def __enter__(self):
            return term_view

        def __exit__(self, *exc_details):
            term_view.tick()
            return False

    class TickerContext(object):
        def __enter__(self):
            return ticker

        def __exit__(self, *exc_details):
            ticker.tick(force=True)
            return False

    ticker = exit_stack.enter_context(TickerContext())
    term_view = exit_stack.enter_context(TermContext())

    runq = runqueue.RunQueue(workers.add, queue_status.listener())

    res = collections.defaultdict(list)

    build_errors = {}
    execution_log = {}

    class TaskContext(object):
        def __init__(self):
            self.task_cache = task_cache.TaskCache(runq)
            self.pattern_cache = task_cache.TaskCache(runq, self.prepare_pattern)
            self.resource_cache = task_cache.TaskCache(runq, self.prepare_resource)
            self.opts = opts
            self.patterns = patterns
            self.resources = resources
            self.runq = runq
            self.state = state
            self.results = results
            self.fetchers_storage = fetchers_storage
            self.build_time_cache = build_time_cache

            import yalibrary.runner.tasks.cache

            self.compact_cache_task = yalibrary.runner.tasks.cache.CompactCacheTask(cache, state, opts, execution_log)
            self.clean_symres_task = yalibrary.runner.tasks.cache.CleanSymresTask(
                symlink_result, state, opts, execution_log
            )
            self.clean_build_task = yalibrary.runner.tasks.cache.CleanBuildRootTask(
                build_root_set, state, opts, execution_log
            )
            self.content_uids = opts.force_content_uids
            logger.debug("content UIDs %s in runner", "*enabled*" if self.content_uids else "*disabled*")

            class SymresLock(object):
                def __enter__(pself):
                    return self.clean_symres_task

                # Shared lock is held till the end of execution.
                def __exit__(pself, *exc_details):
                    lock = self.clean_symres_task.get_shared_lock()
                    if lock:
                        lock.release()
                    return False

            self.clean_symres_task = exit_stack.enter_context(SymresLock())
            save_links_for = getattr(opts, 'save_links_for', [])
            import yalibrary.runner.tasks.prepare

            if self.opts.use_distbuild:
                res_nodes = [x for x in nodes if x.is_result_node]
                if self.opts.output_only_tests:  # XXX
                    # We need to download test's output (testing_output_stuff.tar, etc) from distbuild - use output_uids instead of result_uids
                    res_set = set(sum([t.output_uids for t in ctx.tests], []))
                    res_nodes = [x for x in res_nodes if x.uid in res_set]
                download_test_results = bool(self.opts.run_tests)
                if not opts.download_artifacts and download_test_results:
                    if opts.remove_result_node:
                        save_links_for += get_link_outputs(res_nodes)
                        save_links_for = list(set(save_links_for))
                    else:
                        res_nodes = [n for n in res_nodes if "test_results_node" in n.kv]

                self.results = frozenset([n.uid for n in res_nodes])
                self.prepare_all_nodes_task = yalibrary.runner.tasks.prepare.PrepareAllDistNodesTask(
                    nodes, self, opts.download_artifacts or download_test_results, res_nodes
                )
            else:
                self.prepare_all_nodes_task = yalibrary.runner.tasks.prepare.PrepareAllNodesTask(
                    nodes, self, cache, dist_cache
                )

            self.save_links_regex = (
                re.compile("|".join(fnmatch.translate(e) for e in save_links_for)) if save_links_for else None
            )

            if not opts.use_distbuild:
                import yalibrary.runner.tasks.run

                if opts.local_executor:
                    if create_local_executor:
                        wait_local_executor_init_fn()
                        opts.executor_address = local_executor_address
                    self.executor_type = yalibrary.runner.tasks.run.LocalExecutor
                else:
                    self.executor_type = yalibrary.runner.tasks.run.PopenExecutor

        def fast_fail(self, fatal=False):
            if fatal or not continue_on_fail:

                def stopping():
                    raise EarlyStoppingException()

                state.stopping(stopping)

        def exec_run_node(self, node, parent_task):
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

        def prepare_pattern(self, pattern):
            import yalibrary.runner.tasks.pattern

            # XXX: remove transient_resource_dir needed for $(VCS) pattern only.
            return yalibrary.runner.tasks.pattern.PreparePattern(
                pattern,
                self,
                ctx.res_dir,
                transient_resource_dir,
                resources_map,
                fetchers_storage,
                fetch_resource_if_need,
                execution_log,
            )

        def prepare_resource(self, uri_description):
            import yalibrary.runner.tasks.resource

            return yalibrary.runner.tasks.resource.PrepareResource(
                uri_description,
                self,
                transient_resource_dir,
                fetchers_storage,
                fetch_resource_if_need,
                execution_log,
                cache,
            )

        def prepare_node(self, node):
            import yalibrary.runner.tasks.prepare

            return yalibrary.runner.tasks.prepare.PrepareNodeTask(node, self, cache, dist_cache)

        def restore_from_dist_cache(self, node):
            import yalibrary.runner.tasks.dist_cache

            return yalibrary.runner.tasks.dist_cache.RestoreFromDistCacheTask(
                node, build_root_set, self, dist_cache, fmt_node, execution_log, self.save_links_regex
            )

        def put_in_dist_cache(self, node, build_root):
            import yalibrary.runner.tasks.dist_cache

            return yalibrary.runner.tasks.dist_cache.PutInDistCacheTask(
                node, build_root, dist_cache, opts.yt_store_codec, fmt_node, execution_log
            )

        def restore_from_cache(self, node):
            import yalibrary.runner.tasks.cache

            return yalibrary.runner.tasks.cache.RestoreFromCacheTask(
                node,
                build_root_set.new(node.outputs, node.refcount, node.dir_outputs, compute_hash=node.hashable),
                self,
                cache,
                dist_cache,
                execution_log,
            )

        def put_in_cache(self, node, build_root):
            import yalibrary.runner.tasks.cache

            return yalibrary.runner.tasks.cache.PutInCacheTask(
                node,
                build_root,
                cache,
                opts.cache_codec,
                execution_log,
                dir_outputs_test_mode=self.opts.dir_outputs_test_mode,
            )

        def write_through_caches(self, node, build_root):
            import yalibrary.runner.tasks.cache

            return yalibrary.runner.tasks.cache.WriteThroughCachesTask(node, self, cache, dist_cache, build_root)

        def clear_uid(self, uid):
            cache.clear_uid(uid)

        def result_node(self, node, provider=None):
            import yalibrary.runner.tasks.result

            return yalibrary.runner.tasks.result.ResultNodeTask(
                node,
                self,
                callback,
                need_output,
                output_result,
                need_symlinks,
                symlink_result,
                suppress_outputs_conf,
                install_result,
                bin_result,
                lib_result,
                res,
                fmt_node,
                ctx.cache_test_statuses,
                provider=provider,
            )

        def eager_result(self, provider):
            if not self.opts.eager_execution:
                return

            node = provider._node
            if node.uid in self.results:
                self.runq.add(
                    self.result_node(node, provider),
                    deps=[self.clean_symres_task],
                    inplace_execution=self.opts.eager_execution,
                )

        def run_node(self, node):
            import yalibrary.runner.tasks.run

            return yalibrary.runner.tasks.run.RunNodeTask(
                node,
                build_root_set.new(node.outputs, node.refcount, node.dir_outputs, compute_hash=node.hashable),
                self,
                threads,
                test_threads,
                execution_log,
                build_errors,
                display,
                close_fds,
                None,
                callback,
                cache,
                dist_cache,
                ienv.manager(),
            )

        def run_dist_node(self, node):
            import yalibrary.runner.tasks.distbuild

            return yalibrary.runner.tasks.distbuild.DistDownloadTask(
                node,
                self,
                build_root_set,
                self.patterns,
                self.save_links_regex,
                callback,
                dist_cache,
                self.opts.mds_read_account,
                execution_log,
                dump_evlog_stat=self.opts.evlog_dump_node_stat,
            )

        def dispatch_uid(self, uid, *args, **kwargs):
            node = who_provides[uid]
            try:
                task = self.task_cache(node)
                self.runq.dispatch(task, *args, **kwargs)
            except KeyError:
                pass

        def dispatch_all(self, *args, **kwargs):
            return self.runq.dispatch_all(*args, **kwargs)

        def signal_ready(self):
            # signal distbuild runner that execution graph is ready
            # should dominate all non-exceptional 'returns'
            logger.debug("Local executor is ready")
            ctx.local_runner_ready.set(True)

    task_context = TaskContext()
    ctx.task_context = task_context  # XXX

    def process_queue():
        while True:
            try:
                f = workers.next(timeout=opts.status_refresh_interval, ready_to_stop=lambda: runq.pending == 0)
            except StopIteration:
                break
            ticker.tick(False, task_context.compact_cache_task.fmt_size())
            if f:
                f()

    ienv.start()
    start_time = time.time()
    try:
        runq.add(task_context.clean_symres_task)
        runq.add(task_context.prepare_all_nodes_task)
        runq.add(task_context.compact_cache_task, deps=[task_context.prepare_all_nodes_task])

        if opts.use_distbuild:
            from yalibrary.runner.tasks import distbuild

            wrapped_process_queue = distbuild.wrap_sending_telemetry_for(process_queue)
            wrapped_process_queue()
        else:
            process_queue()

        runq.add(task_context.clean_build_task, deps=[])
        process_queue()

        runq.dispatch_all()
    except EarlyStoppingException:
        pass
    finally:
        exit_stack.pop_all().close()

        if hasattr(cache, 'stats'):
            cache.stats(execution_log)

        if dist_cache and hasattr(dist_cache, 'stats'):
            dist_cache_evlog_writer = app_ctx.evlog.get_writer('yt_store') if getattr(app_ctx, 'evlog', None) else None
            dist_cache.stats(execution_log, dist_cache_evlog_writer)

    wall_time = time.time() - start_time

    if not opts.use_distbuild:
        for node in nodes:
            if node.uid not in execution_log:
                execution_log[node.uid] = {'cached': True}

    replay_info = list(runq.replay())

    exit_code_map = {}
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

    merged_exit_code = core.error.merge_exit_codes([0] + list(exit_code_map.values()))
    logger.debug("Merged exit code: %d", merged_exit_code)

    def calc_critical_path():
        critical_tasks = statcalc.calc_critical(replay_info)
        for task_info in critical_tasks:
            yield {
                'name': str(task_info.task),  # TODO: better
                'timing': task_info.timing,
                'type': task_info.task.short_name(),
            }

    data = statcalc.calc_stat(replay_info)
    data.update(
        {
            'critical_path': list(calc_critical_path()),
            'wall_time': wall_time,
            'build_type': opts.build_type,
            'flags': opts.flags,
            'rel_targets': opts.rel_targets,
            'threads': opts.build_threads,
        }
    )

    logger.debug('Profile of graph execution %s', json.dumps(data))
    core.report.telemetry.report(core.report.ReportTypes.PROFILE_BY_TYPE, data)

    if dist_cache:
        logger.debug('Average compression ratio: %0.2f', dist_cache.avg_compression_ratio)

    # Drop empty entries (with fake/suppressed artifacts)
    res = {k: [x for x in v if x] for k, v in six.iteritems(res)}

    return res, build_errors, merged_exit_code, execution_log, exit_code_map
