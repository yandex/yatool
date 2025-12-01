import sys
import time

import yalibrary.worker_threads as worker_threads
from yalibrary.runner.runner3 import Node
from yalibrary.runner.tasks.enums import WorkerPoolType
from yalibrary.store.dist_store import DistStore
from yalibrary.toolscache import tc_force_gc


class CompactCacheTask(object):
    node_type = 'CompactCache'
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(self, cache, state, opts, execution_log):
        self._cache = cache
        self._state = state
        self._opts = opts
        self._execution_log = execution_log

    def __call__(self, *args, **kwargs):
        if hasattr(self._cache, 'compact'):
            start_time = time.time()
            self._cache.compact(getattr(self._opts, 'new_store_ttl'), getattr(self._opts, 'cache_size'), self._state)
            self._execution_log["compact cache"] = {'timing': (start_time, time.time()), 'prepare': '', 'type': 'clean'}

            start_time = time.time()
            tc_force_gc(getattr(self._opts, 'cache_size'))
            self._execution_log['compact_tc_cache'] = {
                'timing': (start_time, time.time()),
                'prepare': '',
                'type': 'clean',
            }

    def fmt_size(self):
        def sizeof_fmt(num, suffix='B'):
            for unit in ['', 'Ki', 'Mi', 'Gi', 'Ti', 'Pi', 'Ei', 'Zi']:
                if abs(num) < 1024.0:
                    return "[[imp]]{:.1f}[[rst]]{}{}".format(num, unit, suffix)
                num /= 1024.0
            raise Exception('too big')

        try:
            return sizeof_fmt(self._cache.size())
        except AttributeError:
            return ''

    def __str__(self):
        return 'CompactCache'

    def prio(self):
        return sys.maxsize

    def res(self):
        return worker_threads.ResInfo()

    def short_name(self):
        return 'compact_cache'

    def status(self):
        return '[[c:yellow]]COMPACTING CACHE[[rst]] ' + self.fmt_size()


class CleanSymresTask(object):
    node_type = 'CleanSymres'
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(self, symlink_store, state, opts, execution_log):
        self._symlink_store = symlink_store
        self._state = state
        self._opts = opts
        self._shared_lock = None
        self._execution_log = execution_log

    def __call__(self, *args, **kwargs):
        if not self._symlink_store:
            return

        if not hasattr(self._symlink_store, 'sieve'):
            return

        start_time = time.time()
        try:
            from exts.plocker import Lock, LOCK_EX, LOCK_SH, LOCK_NB, LockException

            if self._opts.strip_symlinks or self._opts.auto_clean_results_cache:
                # Blocking only in non-automatic mode.
                timeout = 1000000000 if self._opts.strip_symlinks else 2
                try:
                    with Lock(self._symlink_store.file_lock_name, mode='w', timeout=timeout, flags=LOCK_EX | LOCK_NB):
                        self._symlink_store.sieve(self._state, self._opts.symlinks_ttl, self._opts.strip_symlinks)
                except LockException:
                    pass
        finally:
            self._shared_lock = Lock(
                self._symlink_store.file_lock_name, mode='w', timeout=1000000000, flags=LOCK_SH | LOCK_NB
            )
            # released in runner3.py
            self._shared_lock.acquire()
            self._execution_log["clean symres"] = {'timing': (start_time, time.time()), 'prepare': '', 'type': 'clean'}

    def __str__(self):
        return 'CleanSymres'

    def prio(self):
        return sys.maxsize

    def res(self):
        return worker_threads.ResInfo()

    def short_name(self):
        return 'clean_symres'

    def status(self):
        return '[[c:yellow]]CLEANING SYMRES[[rst]]'

    def get_shared_lock(self):
        return self._shared_lock


class CleanBuildRootTask(object):
    node_type = 'CleanBuildRoot'
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(self, build_root_set, state, opts, execution_log):
        self._build_root_set = build_root_set
        self._state = state
        self._opts = opts
        self._execution_log = execution_log

    def __call__(self, *args, **kwargs):
        # XXX: build_root_set cleanup
        if self._opts.run_tests > 0:
            self._build_root_set.stats()
            return

        start_time = time.time()
        self._build_root_set.cleanup()
        self._execution_log["clean temp build dir"] = {
            'timing': (start_time, time.time()),
            'prepare': '',
            'type': 'clean',
        }

    def __str__(self):
        return 'CleanBuildRoot'

    def prio(self):
        return sys.maxsize

    def res(self):
        return worker_threads.ResInfo()

    def short_name(self):
        return 'clean_build_root'

    def status(self):
        return '[[c:yellow]]CLEANING BUILD ROOT[[rst]]'


class PutInCacheTask(object):
    node_type = 'PutInCache'
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(self, node, build_root, cache, cache_codec, execution_log, dir_outputs_test_mode=False):
        self._node = node
        self._build_root = build_root
        self._cache = cache
        self._cache_codec = cache_codec
        self._execution_log = execution_log
        self._dir_outputs_test_mode = dir_outputs_test_mode

    def __call__(self, *args, **kwargs):
        start_time = time.time()
        try:
            codec = None if self._node.target_properties else self._cache_codec
            if self._node.dir_outputs:
                caching_outputs = set(list(self._build_root.output) + self._build_root.dir_outputs_files)
            else:
                caching_outputs = set(self._build_root.output)
            file_list = set()
            digests = self._node.output_digests.file_digests if self._node.output_digests else {}
            for output in caching_outputs:
                fi = self._FileInfo(output)
                fi.digest = digests.get(output)
                file_list.add(fi)
            self._cache.put(self._node.uid, self._build_root.path, file_list, codec)
            if self._node.content_uid is not None:
                self._cache.put(self._node.content_uid, self._build_root.path, file_list, codec)
            if hasattr(self._cache, 'put_dependencies'):
                self._cache.put_dependencies(self._node.uid, self._node.deps)
        finally:
            self._build_root.dec()
        self._execution_log[str(self)] = {
            'timing': (start_time, time.time()),
            'prepare': '',
            'type': 'put into local cache, clean build dir',
        }

    def __str__(self):
        return 'PutInCache({})'.format(self._node.uid)

    def res(self):
        return worker_threads.ResInfo(io=1)

    def prio(self):
        return 0

    @property
    def max_dist(self):
        return self._node.max_dist

    def short_name(self):
        return 'put_in_cache[{}]'.format(self._node.kv.get('p', '??'))

    class _FileInfo(str):
        pass


class DistCacheMixin:
    def __init__(self, dist_cache: DistStore, node: Node, result_only: bool):
        self.__dist_cache = dist_cache
        self.__node = node
        self.__result_only = result_only

    def should_put_in_dist_cache(self) -> bool:
        return (
            self.dist_cache_exists_and_writable()
            and (not self.__result_only or self.__node.is_result_node)
            and self.__dist_cache.fits(self._node)
            and not self.__dist_cache.has(self.__node.uid)
        )

    def dist_cache_exists_and_writable(self) -> bool:
        return self.__dist_cache and not self.__dist_cache.readonly()


class RestoreFromCacheTask(DistCacheMixin):
    node_type = 'RestoreFromCache'
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(self, node, build_root, ctx, cache, dist_cache, execution_log):
        super().__init__(dist_cache, node, ctx.opts.yt_replace_result)
        self._node = node
        self._build_root = build_root
        self._ctx = ctx
        self._cache = cache
        self._execution_log = execution_log

    @property
    def build_root(self):
        return self._build_root

    @property
    def uid(self):
        return self._node.uid

    def __call__(self, *args, **kwargs):
        start_time = time.time()
        self._build_root.create()

        if self._cache.try_restore(self._node.uid, self._build_root.path):
            if self._ctx.opts.dir_outputs_test_mode and self._ctx.opts.runner_dir_outputs:
                self._build_root.propagate_dir_outputs()
            self._build_root.validate()

            if self._ctx.content_uids:
                self._node.output_digests = self._build_root.read_output_digests()

            if self.should_put_in_dist_cache():
                self._build_root.inc()
                self._ctx.runq.add(
                    self._ctx.put_in_dist_cache(self._node, self._build_root),
                    deps=[],
                    inplace_execution=self._ctx.opts.eager_execution,
                )

            self._ctx.eager_result(self)
        else:
            self._ctx.exec_run_node(self._node, self)
        self._execution_log[str(self)] = {
            'timing': (start_time, time.time()),
            'prepare': '',
            'type': 'get from local cache',
        }

    def __str__(self):
        return 'FromCache({})'.format(str(self._node))

    def prio(self):
        return 0

    @property
    def max_dist(self):
        return self._node.max_dist

    def res(self):
        return worker_threads.ResInfo(cpu=1)

    def short_name(self):
        return 'restore[{}]'.format(self._node.kv.get('p', '??'))


class WriteThroughCachesTask(DistCacheMixin):
    node_type = 'WriteThroughCaches'
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(self, node, ctx, cache, dist_cache, build_root):
        super().__init__(dist_cache, node, ctx.opts.yt_replace_result)
        self._node = node
        self._ctx = ctx
        self._cache = cache
        self._build_root = build_root

    def __call__(self, *args, **kwargs):
        if self._ctx.opts.yt_store_wt or not self.dist_cache_exists_and_writable():
            self._ctx.runq.add(
                self._ctx.put_in_cache(self._node, self._build_root),
                deps=[],
                inplace_execution=self._ctx.opts.eager_execution,
            )
        else:
            self._build_root.dec()

        if self.should_put_in_dist_cache():
            self._ctx.runq.add(
                self._ctx.put_in_dist_cache(self._node, self._build_root),
                deps=[],
                inplace_execution=self._ctx.opts.eager_execution,
            )
        else:
            self._build_root.dec()

    def __str__(self):
        return 'WriteThroughCaches({})'.format(self._node.uid)

    def res(self):
        return worker_threads.ResInfo()

    def prio(self):
        return 0

    @property
    def max_dist(self):
        return self._node.max_dist

    def short_name(self):
        return 'write_through_caches[{}]'.format(self._node.kv.get('p', '??'))
