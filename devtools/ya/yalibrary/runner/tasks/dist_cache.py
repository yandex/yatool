import os
import time

import core.error
import yalibrary.worker_threads as worker_threads


def get_link_filename(filename):
    return filename + '.link'


class PutInDistCacheTask(object):
    node_type = 'PutInDistCache'

    def __init__(self, node, build_root, dist_cache, dist_cache_codec, fmt_node, execution_log):
        self._node = node
        self._build_root = build_root
        self._dist_cache = dist_cache
        self._dist_cache_codec = dist_cache_codec
        self._fmt_node = fmt_node
        self._ok = True
        self._skipped = False
        self._execution_log = execution_log

    def __call__(self, *args, **kwargs):
        start_time = time.time()
        try:
            status = self._dist_cache.put(
                self._node.uid, self._build_root.path, list(self._build_root.output), codec=self._dist_cache_codec
            )
            if status.skipped:
                self._skipped = True
            else:
                self._ok = status.ok
        finally:
            self._build_root.dec()
        self._execution_log[str(self)] = {
            'timing': (start_time, time.time()),
            'prepare': '',
            'type': 'put to dist cache',
        }

    def __str__(self):
        return 'PutInDistCache({})'.format(self._node.uid)

    def res(self):
        return worker_threads.ResInfo(upload=1)

    def prio(self):
        return self._node.max_dist

    def short_name(self):
        return 'put_in_dist_cache[{}]'.format(self._node.kv.get('p', '??'))

    def status(self):
        tags = ['[[c:yellow]]{}_UPLOAD[[rst]]'.format(self._dist_cache.tag())]
        if self._skipped:
            tags.append('[[unimp]]SKIPPED[[rst]]')
        elif not self._ok:
            tags.append('[[bad]]FAILED[[rst]]')
        return self._fmt_node(self._node, tags)


class RestoreFromDistCacheTask(object):
    node_type = 'RestoreFromDistCache'

    def __init__(self, node, build_root_set, ctx, dist_cache, fmt_node, execution_log, save_links_for_files):
        self._node = node
        self._ctx = ctx
        self._dist_cache = dist_cache
        self._fmt_node = fmt_node
        self._ok = True
        self._exit_code = 0
        self._execution_log = execution_log
        self._save_links_regex = save_links_for_files
        self._outputs = node.outputs
        self._filter = None

        if save_links_for_files:
            self._fix_outputs_for_links()
            self._filter = self._save_links_filter
        self._build_root = build_root_set.new(
            self._outputs, node.refcount, dir_outputs=node.dir_outputs, compute_hash=node.hashable
        )

    def _fix_outputs_for_links(self):
        outputs = []
        for output in self._node.outputs:
            if self._save_links_regex.match(os.path.basename(output)):
                outputs.append(get_link_filename(output))
            else:
                outputs.append(output)

        self._outputs = outputs

    def _save_links_filter(self, file_path, url):
        if self._save_links_regex.match(os.path.basename(file_path)):
            dirname = os.path.dirname(file_path)
            if not os.path.exists(dirname):
                os.makedirs(dirname)

            with open(get_link_filename(file_path), 'w') as link_file:
                link_file.write(url)
            return True
        return False

    @property
    def uid(self):
        return self._node.uid

    @property
    def build_root(self):
        return self._build_root

    @property
    def exit_code(self):
        return self._exit_code

    def __call__(self, *args, **kwargs):
        start_time = time.time()
        self._build_root.create()

        if self._ctx.opts.dist_cache_late_fetch:
            is_ok = self._dist_cache.has(self._node.uid) and self._dist_cache.try_restore(
                self._node.uid, self._build_root.path, self._filter
            )
        else:
            is_ok = self._dist_cache.try_restore(self._node.uid, self._build_root.path, self._filter)

        if is_ok:
            if self._ctx.content_uids:
                # If hashes file is not cached remotely it will be recomputed and cached locally
                # otherwise it assumed to be among outputs and will be processed as such
                self._node.output_digests = self._build_root.read_output_digests(write_if_absent=True)

            self._build_root.validate()
            # if _dist_cache is not read-only, i.e. we are heating it, then do not pollute local cache
            if self._ctx.opts.yt_store_wt or self._dist_cache.readonly():
                self._build_root.inc()
                if self._ctx.opts.dir_outputs_test_mode and self._node.dir_outputs:
                    self._build_root.extract_dir_outputs()
                self._ctx.runq.add(self._ctx.put_in_cache(self._node, self._build_root), deps=[])

            self._ctx.eager_result(self)
        else:
            self._ok = False
            if self._ctx.opts.yt_store_exclusive:
                # Restoration failed, but we know that this node is presented
                # (at least was presented at preparation stage) in the distributed cache.
                # Treat this problem as infra error which may be fixed with retry.
                self._exit_code = core.error.ExitCodes.INFRASTRUCTURE_ERROR
                self._ctx.fast_fail()
            else:
                self._ctx.exec_run_node(self._node, self)
        self._execution_log[str(self)] = {
            'timing': (start_time, time.time()),
            'prepare': '',
            'type': 'get from dist cache',
        }

    def __str__(self):
        return 'FromDistCache({})'.format(str(self._node))

    def prio(self):
        return self._node.max_dist

    def res(self):
        return worker_threads.ResInfo(download=1)

    def short_name(self):
        return 'restore_from_dist_cache[{}]'.format(self._node.kv.get('p', '??'))

    def status(self):
        tags = ['[[c:green]]{}_DOWNLOAD[[rst]]'.format(self._dist_cache.tag())]
        if not self._ok:
            tags.append('[[bad]]FAILED[[rst]]')
        return self._fmt_node(self._node, tags)
