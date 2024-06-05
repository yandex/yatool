import os
import sys
import logging
import time

import core.error

from yalibrary.fetcher.uri_parser import parse_resource_uri
from yalibrary.fetcher import common as fetcher_common
from yalibrary.active_state import Cancelled
import yalibrary.worker_threads as worker_threads

import exts.yjson as json
import exts.fs as fs


class _ExternalResource(object):
    @staticmethod
    def get_sb_id(arc_root, uri):
        resource_type, address = uri.split(':', 1)

        with open(os.path.join(arc_root, address), 'r') as f:
            j = json.load(f)

        if j.get('storage', '') != 'SANDBOX' or 'resource_id' not in j:
            return None

        return j['resource_id']


class PrepareResource(object):
    def __init__(
        self, uri_description, ctx, resource_root, fetchers_storage, fetch_resource_if_need, execution_log, cache
    ):
        self._uri_description = dict(uri_description)
        self._ctx = ctx
        self._fetchers_storage = fetchers_storage
        self._fetch_resource_if_need = fetch_resource_if_need

        self._percent = None
        self._error = None
        self._exit_code = 0
        self._resource_root = resource_root
        self._execution_log = execution_log
        self._cache = cache

    @property
    def exit_code(self):
        return self._exit_code

    @staticmethod
    def dep_resources(ctx, x):
        uri = x['uri']
        resource_type, address = uri.split(':', 1)

        accepted_resource_types = {'ext'} | ctx.fetchers_storage.accepted_schemas()

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
            self._exit_code = core.error.ExitCodes.INFRASTRUCTURE_ERROR if core.error.is_temporary_error(e) else 1

            self._ctx.fast_fail()

            self._error = '[[bad]]ERROR[[rst]]: ' + str(e)

    def fetch(self):
        uri = self._uri_description['uri']
        accepted_resource_types = {'ext'} | self._fetchers_storage.accepted_schemas()
        parsed_uri = parse_resource_uri(uri, accepted_resource_types)
        resource_type = parsed_uri.resource_type

        logging.debug("accepted_resource_types={}".format(repr(accepted_resource_types)))

        assert resource_type in accepted_resource_types, 'Resource schema {} not in accepted ({})'.format(
            resource_type, ', '.join(sorted(accepted_resource_types))
        )

        def progress_callback(percent):
            self._ctx.state.check_cancel_state()
            self._percent = percent

        if resource_type in self._fetchers_storage.accepted_schemas():
            cacheable = not self._ctx.opts.clear_build
            resource_type_root = os.path.join(self._resource_root, resource_type)

            found_in_local_cache = cacheable and self._cache.has(self.uid)
            if found_in_local_cache:
                result_path = os.path.join(resource_type_root, parsed_uri.resource_id)
                if self._cache.try_restore(self.uid, result_path):
                    return result_path

            result = os.path.abspath(
                self._fetch_resource_if_need(
                    self._fetchers_storage.get_by_type(resource_type),
                    resource_type_root,
                    uri,
                    progress_callback,
                    self._ctx.state,
                    install_params=(fetcher_common.FIXED_NAME, False),
                    keep_directory_packed=True,
                )
            )

            files = list([os.path.join(result, file_name) for file_name in os.listdir(result)])
            self._cache.put(self.uid, result, files)

            return result

        elif resource_type == 'ext':
            sb_resource_id = _ExternalResource.get_sb_id(self._ctx.opts.arc_root, uri)

            if sb_resource_id is None:
                return None

            sb_out = os.path.join(self._resource_root, 'sbr', str(sb_resource_id))

            dest_file_name, _ = os.path.splitext(parsed_uri.resource_id)
            final_dest = os.path.join(self._resource_root, resource_type, dest_file_name)
            logging.debug("Copy sbr: file {} to ext: {}".format(os.path.join(sb_out, 'resource'), final_dest))

            fs.ensure_dir(os.path.dirname(final_dest))
            fs.hardlink_or_copy(os.path.join(sb_out, 'resource'), final_dest)
            return final_dest

    def __str__(self):
        return 'Resource(' + self._uri_description['uri'] + ')'

    def prio(self):
        return sys.maxsize

    def body(self):
        return self._error

    def status(self):
        str = '[[c:yellow]]RESOURCE[[rst]] ' + '[[imp]]$(' + self._uri_description['uri'][0:20] + ')[[rst]]'
        if self._percent is not None:
            str += ' - %.1f%%' % self._percent
        return str

    def res(self):
        return worker_threads.ResInfo()

    def short_name(self):
        return 'resource[{}]'.format(self._uri_description['uri'][0:20])

    @property
    def uid(self):
        return 'prepare-resource-{}'.format(self._uri_description['uri'])
