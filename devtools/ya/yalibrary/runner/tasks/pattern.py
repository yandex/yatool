import os
import sys
import logging
import base64
import tempfile
import time

import six

import devtools.ya.core.error

from devtools.ya.yalibrary.active_state import Cancelled
from yalibrary.fetcher import resource_fetcher
from yalibrary.runner.tasks.enums import WorkerPoolType
import yalibrary.worker_threads as worker_threads

import yalibrary.fetcher.progress_info as progress_info_lib


class PreparePattern(object):
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(
        self,
        pattern,
        ctx,
        res_dir,
        build_root,
        resources_map,
        legacy_sandbox_fetcher,
        fetch_resource_if_need,
        execution_log,
    ):
        self._pattern = pattern
        self._ctx = ctx
        self._res_dir = res_dir
        self._resources_map = resources_map
        self._legacy_sandbox_fetcher = legacy_sandbox_fetcher
        self._fetch_resource_if_need = fetch_resource_if_need

        self._progress_info = progress_info_lib.ProgressInfo()
        self._error = None
        self._exit_code = 0
        self._build_root = build_root
        self._execution_log = execution_log
        self._shloud_use_universal_fetcher = getattr(ctx.opts, 'use_universal_fetcher_everywhere', False)

    @property
    def exit_code(self):
        return self._exit_code

    def __call__(self, *args, **kwargs):
        try:
            start_time = time.time()
            self._ctx.patterns[self._pattern] = self.fetch(self._resources_map[self._pattern])
            finish_time = time.time()
            self._execution_log["$({})".format(self._pattern)] = {
                'timing': (start_time, finish_time),
                'prepare': '',
                'type': 'tools',
            }
        except Cancelled:
            logging.debug("Fetching of the %s resource was cancelled", self._pattern)
            self._ctx.fast_fail()
            raise
        except Exception as e:
            if getattr(e, 'mute', False) is not True:
                logging.exception('Unable to fetch resource %s', self._pattern)

            self._exit_code = (
                devtools.ya.core.error.ExitCodes.INFRASTRUCTURE_ERROR
                if devtools.ya.core.error.is_temporary_error(e)
                else 1
            )

            self._ctx.fast_fail()

            self._error = '[[bad]]ERROR[[rst]]: ' + str(e)

    def fetch(self, item):
        platform = getattr(self._ctx.opts, 'host_platform', None)
        resource_desc = resource_fetcher.select_resource(item, platform)
        resource = resource_desc['resource']
        resource_type, resource_id = resource.split(':', 1)
        if resource_type == 'https':
            resource_type = 'http'

        strip_prefix = resource_desc.get('strip_prefix')

        if resource_type in frozenset(['sbr', 'http', 'https', 'docker']):

            def progress_callback(downloaded, total_size):
                if not self._shloud_use_universal_fetcher:
                    self._ctx.state.check_cancel_state()

                self._progress_info.set_total(total_size)
                self._progress_info.update_downloaded(downloaded)

            return os.path.abspath(
                self._fetch_resource_if_need(
                    self._legacy_sandbox_fetcher,
                    self._res_dir,
                    resource,
                    progress_callback,
                    self._ctx.state,
                    strip_prefix=strip_prefix,
                )
            )
        elif resource_type == 'file':
            return os.path.abspath(resource_id)
        elif resource_type == 'base64':
            dir_name = tempfile.mkdtemp(prefix="base64_resource-", dir=self._build_root)
            base_name, contents = resource_id.split(':', 1)
            with open(os.path.join(dir_name, base_name), 'w') as c:
                c.write(six.ensure_str(base64.b64decode(contents)))
            return dir_name
        else:
            raise RuntimeError(f"Unexpected resource type: {resource_type}")

    def __str__(self):
        return 'Pattern(' + self._pattern + ')'

    def prio(self):
        return sys.maxsize

    def body(self):
        return self._error

    def status(self):
        res = '[[c:yellow]]PREPARE[[rst]] ' + '[[imp]]$(' + self._pattern + ')[[rst]]'
        if self._progress_info.downloaded > 0:
            res += ' - %s' % self._progress_info.pretty_progress
        return res

    def res(self):
        return worker_threads.ResInfo()

    def short_name(self):
        return 'pattern[{}]'.format(self._pattern)
