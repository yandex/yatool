import os
import sys
import logging
import time

import devtools.ya.core.error
import yalibrary.worker_threads as worker_threads

from devtools.ya.yalibrary.active_state import Cancelled
from yalibrary.fetcher import common as fetcher_common
from yalibrary.fetcher.uri_parser import parse_resource_uri
from yalibrary.runner.tasks.enums import WorkerPoolType

import exts.yjson as json
import exts.fs as fs

import yalibrary.fetcher.progress_info as progress_info_lib

from .resource_display import ResourceDisplay


class _ExternalResource(object):
    @staticmethod
    def get_sb_id(arc_root, uri):
        _, address = uri.split(':', 1)

        with open(os.path.join(arc_root, address), 'r') as f:
            j = json.load(f)

        if j.get('storage', '') != 'SANDBOX' or 'resource_id' not in j:
            return None

        return j['resource_id']


class PrepareResource(object):
    worker_pool_type = WorkerPoolType.SERVICE

    def __init__(
        self, uri_description, ctx, resource_root, legacy_sandbox_fetcher, fetch_resource_if_need, execution_log, cache
    ):
        self._uri_description = dict(uri_description)
        self._ctx = ctx
        self._legacy_sandbox_fetcher = legacy_sandbox_fetcher
        self._fetch_resource_if_need = fetch_resource_if_need

        self._progress_info = progress_info_lib.ProgressInfo()
        self._error = None
        self._exit_code = 0
        self._resource_root = resource_root
        self._execution_log = execution_log
        self._cache = cache

        self._parsed_uri = None
        self._resource_display = None
        self._shloud_use_universal_fetcher = getattr(ctx.opts, 'use_universal_fetcher_everywhere', False)

    @property
    def parsed_uri(self):
        if not self._parsed_uri:
            uri = self._uri_description['uri']
            accepted_resource_types = frozenset(['sbr', 'http', 'https', 'ext', 'docker'])
            self._parsed_uri = parse_resource_uri(uri, accepted_resource_types)
        return self._parsed_uri

    @property
    def resource_display(self):
        # type: () -> ResourceDisplay
        if not self._resource_display:
            self._resource_display = ResourceDisplay.create(self.parsed_uri)
        return self._resource_display

    @property
    def exit_code(self):
        return self._exit_code

    @staticmethod
    def dep_resources(ctx, x):
        uri = x['uri']
        resource_type, _ = uri.split(':', 1)

        accepted_resource_types = frozenset(['sbr', 'http', 'https', 'ext', 'docker'])

        assert resource_type in accepted_resource_types, 'Resource schema {} not in accepted ({})'.format(
            resource_type, ', '.join(sorted(accepted_resource_types))
        )

        if resource_type != 'ext':
            return None

        sb_resource_id = _ExternalResource.get_sb_id(ctx.opts.arc_root, uri)

        if sb_resource_id is None:
            return None

        return [ctx.resource_cache((('uri', 'sbr:{}'.format(sb_resource_id)),))]

    def __call__(self, *args, **kwargs):
        try:
            start_time = time.time()
            path = self.fetch()
            logging.debug('Fetched resource %s to %s', self._uri_description, path)
            uri = self._uri_description['uri']
            if uri in self._ctx.resources:
                raise Exception(
                    "Inconsistent parameters for resources %s, %s", self._uri_description, self._ctx.resources[uri]
                )
            self._ctx.resources[uri] = self._uri_description
            finish_time = time.time()
            self._execution_log[uri] = {
                'timing': (start_time, finish_time),
                'prepare': '',
                'type': 'resources',
            }
        except Cancelled:
            logging.debug("Fetching of the %s resource was cancelled", self._uri_description)
            self._ctx.fast_fail()
            raise
        except Exception as e:
            logging.exception('Unable to fetch resource %s', self._uri_description)
            self._exit_code = (
                devtools.ya.core.error.ExitCodes.INFRASTRUCTURE_ERROR
                if devtools.ya.core.error.is_temporary_error(e)
                else 1
            )

            self._ctx.fast_fail()

            self._error = '[[bad]]ERROR[[rst]]: ' + str(e)

    def fetch(self):
        accepted_resource_types = frozenset(['sbr', 'http', 'https', 'ext', 'docker'])
        resource_type = self.parsed_uri.resource_type

        assert resource_type in accepted_resource_types, 'Resource schema {} not in accepted ({})'.format(
            resource_type, ', '.join(sorted(accepted_resource_types))
        )

        def progress_callback(downloaded, total_size):
            if not self._shloud_use_universal_fetcher:
                self._ctx.state.check_cancel_state()

            self._progress_info.set_total(total_size)
            self._progress_info.update_downloaded(downloaded)

        if resource_type in frozenset(['sbr', 'http', 'https', 'docker']):
            cacheable = not self._ctx.opts.clear_build
            resource_type_root = os.path.join(self._resource_root, resource_type)

            found_in_local_cache = cacheable and self._cache.has(self.uid)
            if found_in_local_cache:
                result_path = os.path.join(resource_type_root, self.parsed_uri.resource_id)
                if self._cache.try_restore(self.uid, result_path):
                    return result_path

            res = self._fetch_resource_if_need(
                self._legacy_sandbox_fetcher,
                resource_type_root,
                self._uri_description['uri'],
                progress_callback,
                self._ctx.state,
                install_params=(fetcher_common.FIXED_NAME, False),
                keep_directory_packed=True,
                force_universal_fetcher=self._shloud_use_universal_fetcher,
            )
            where = os.path.abspath(res.where)

            files = list([os.path.join(where, file_name) for file_name in os.listdir(where)])
            self._cache.put(self.uid, where, files)

            return where

        elif resource_type == 'ext':
            sb_resource_id = _ExternalResource.get_sb_id(self._ctx.opts.arc_root, self._uri_description['uri'])

            if sb_resource_id is None:
                return None

            sb_out = os.path.join(self._resource_root, 'sbr', str(sb_resource_id))

            dest_file_name, _ = os.path.splitext(self.parsed_uri.resource_id)
            final_dest = os.path.join(self._resource_root, resource_type, dest_file_name)
            logging.debug("Copy sbr: file {} to ext: {}".format(os.path.join(sb_out, 'resource'), final_dest))

            fs.ensure_dir(os.path.dirname(final_dest))
            fs.hardlink_or_copy(os.path.join(sb_out, 'resource'), final_dest)
            return final_dest
        else:
            raise RuntimeError(f"Unexpected resource type: {resource_type}")

    def __str__(self):
        return 'Resource(' + self._uri_description['uri'] + ')'

    def prio(self):
        return sys.maxsize

    def body(self):
        return self._error

    def status(self):
        display = self.resource_display.get_short_display(color=True)
        res = '[[c:yellow]]RESOURCE[[rst]] [[imp]]$(' + display + '[[imp]])[[rst]]'
        if self._progress_info.downloaded > 0:
            res += ' - %s' % self._progress_info.pretty_progress
        return res

    def res(self):
        return worker_threads.ResInfo()

    def short_name(self):
        return 'resource[{}]'.format(self.resource_display.get_short_display())

    @property
    def uid(self):
        return 'prepare-resource-{}'.format(self._uri_description['uri'])
