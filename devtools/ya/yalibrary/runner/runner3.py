import collections
import contextlib2
import exts.yjson as json
import fnmatch
import logging
import os
import re
import time
import weakref

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
import test.const

from yalibrary import status_view
from yalibrary.active_state import Cancelled
from yalibrary.fetcher.resource_fetcher import fetch_resource_if_need
from yalibrary.runner import build_root
from yalibrary.runner import patterns as ptn
from yalibrary.runner import runqueue
from yalibrary.runner import statcalc
from yalibrary.runner import worker_threads
from yalibrary.runner import task_cache
from yalibrary.runner.command_file.python import command_file as cf
import yalibrary.runner.sandboxing as sandboxing
from yalibrary.status_view.helpers import format_paths


logger = logging.getLogger(__name__)


def calc_once(func):
    def f(self):
        try:
            self.__cache
        except AttributeError:
            self.__cache = weakref.WeakKeyDictionary()

        try:
            return self.__cache[func]
        except KeyError:
            self.__cache[func] = func(self)

        return self.__cache[func]

    return f


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
            if not o.endswith(test.const.TRACE_FILE_NAME):
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

    ienv = sandboxing.FuseSandboxing(opts, ctx.src_dir)

    class IEnvContext(object):
        def __enter__(self):
            return ienv

        def __exit__(self, *exc_details):
            ienv.stop()
            return False

    ienv = exit_stack.enter_context(IEnvContext())

    test_threads = ctx.opts.test_threads or threads
    net_threads = ctx.opts.yt_store_threads or 3
    io_limit = min(ctx.opts.link_threads or 2, threads)
    cap = worker_threads.ResInfo(
        io=io_limit, cpu=threads, test=test_threads, download=threads + net_threads, upload=net_threads
    )

    worker_threads_count = threads + 1 + net_threads
    workers = worker_threads.WorkerThreads(
        state, worker_threads_count, worker_threads.ResInfo(), cap, getattr(app_ctx, 'evlog', None)
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
    elif ctx.opts.oauth_token:

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

    class Noda(object):
        def __init__(self, kwargs):
            self.args = kwargs
            self.uid = kwargs.get('uid')
            self.self_uid = kwargs.get('self_uid', None)
            # FIXME: we need separate property in the node for this
            self.hashable = True if self.self_uid else False
            self.content_uid = None
            self.outputs_uid = None
            self.inputs = kwargs.get('inputs')
            self.outputs = kwargs.get('outputs')
            self.resources = kwargs.get('resources', [])
            self.tared_outputs = kwargs.get('tared_outputs', [])
            self.dir_outputs = kwargs.get('dir_outputs', [])
            self.tags = kwargs.get('tags', [])
            self.deps = kwargs.get('deps')
            self.kv = kwargs.get('kv')
            self.requirements = kwargs.get('requirements', {})
            self.cacheable = kwargs.get('cache', True)
            self.priority = kwargs.get('priority')
            self.target_properties = kwargs.get('target_properties', {})
            self.max_dist = 0
            self.ignore_broken_dependencies = kwargs.get('ignore_broken_dependencies', False)
            self.refcount = 0
            self.stable_dir_outputs = kwargs.get('stable_dir_outputs', False)

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
            p = patterns.sub()
            p['BUILD_ROOT'] = exts.windows.win_path_fix(build_root)
            return self._command_args(build_root, p)

        def prio(self):
            return self.priority or 0

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

        @calc_once
        def is_result_node(self):
            return self.uid in results

        @calc_once
        def dep_nodes(self):
            return [who_provides[d] for d in self.consume()]

    nodes = [Noda(x) for x in graph['graph']]

    timer.show_step('build nodes')

    results = frozenset(graph['result'])

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
        queue_status, display, ninja, opts.ext_progress, False, output_replacements=output_replacements
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

            if not opts.use_distbuild:
                import yalibrary.runner.tasks.run

                if opts.local_executor:
                    if not opts.executor_address:
                        from devtools.executor.python import executor

                        # Don't cache_stderr to avoid belated reading of special tags
                        _, opts.executor_address = executor.start_executor(
                            cache_stderr=False, debug=core.config.is_test_mode()
                        )
                    self.executor_type = yalibrary.runner.tasks.run.LocalExecutor
                else:
                    self.executor_type = yalibrary.runner.tasks.run.PopenExecutor

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
                res_nodes = [x for x in nodes if x.is_result_node()]
                if self.opts.output_only_tests:  # XXX
                    # We need to download test's output (testing_output_stuff.tar, etc) from distbuild - use output_uids instead of result_uids
                    res_set = set(sum([t.output_uids for t in ctx.tests], []))
                    res_nodes = [x for x in nodes if x.is_result_node() and x.uid in res_set]
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
