import json
import logging
import os
import time

from contextlib2 import ExitStack
from devtools.ya.core import config as core_config
from devtools.ya.core import monitoring as core_monitoring
from devtools.ya.core import report
from exts import archive, asyncthread, fs, tmp
from exts.timer import AccumulateTime
from library.python import compress
from yalibrary.store.dist_store import DistStore
from yalibrary.store.yt_store import client, consts, retries, utils

logger = logging.getLogger(__name__)
# Suppress spam
compress.logger.setLevel(logging.INFO)


class YtInitException(Exception):
    pass


class YtStore(DistStore):
    def __init__(
        self,
        proxy,
        data_dir,
        cache_filter,
        token=None,
        readonly=False,
        create_tables=False,
        max_cache_size=None,
        ttl=None,
        data_table_name=consts.YT_CACHE_DEFAULT_DATA_TABLE_NAME,
        metadata_table_name=consts.YT_CACHE_DEFAULT_METADATA_TABLE_NAME,
        stat_table_name=consts.YT_CACHE_DEFAULT_STAT_TABLE_NAME,
        max_file_size=0,
        fits_filter=None,
        heater_mode=False,
        with_self_uid=False,
        probe_before_put=False,
        probe_before_put_min_size=0,
        retry_time_limit=None,
        sync_transaction=None,
        **kwargs
    ):
        super(YtStore, self).__init__(
            name='yt-store',
            stats_name='yt_store_stats',
            tag='YT',
            readonly=readonly,
            max_file_size=max_file_size,
            fits_filter=fits_filter,
            heater_mode=heater_mode,
        )
        self._proxy = proxy
        self._ttl = ttl
        self._cache_filter = cache_filter
        self._probe_before_put = probe_before_put
        self._probe_before_put_min_size = probe_before_put_min_size

        self._data_dir = data_dir

        data_dir = data_dir.rstrip('/')
        data_table = data_dir + "/" + data_table_name
        metadata_table = data_dir + "/" + metadata_table_name
        stat_table = data_dir + "/" + stat_table_name

        self._total_compressed_size = 0
        self._total_raw_size = 0
        self._client = client.YtStoreClient(
            proxy,
            data_table,
            metadata_table,
            stat_table,
            token=token,
            retry_policy=retries.RetryPolicy(
                on_error_callback=self._on_error_callback_for_retries_wrapper, retry_time_limit=retry_time_limit
            ),
            sync_transaction=sync_transaction,
        )

        self._time_to_first_recv_meta = None
        self._time_to_first_call_has = None

        self._disabled = False
        self._prepare_future = None
        self._prepare_tables_future = None

        self._yt_store_exclusive = kwargs.get('yt_store_exclusive', False)
        self._retry_time_limit = retry_time_limit

        if self._heater_mode:
            self._prepare_tables(create_tables, readonly, proxy, ttl, with_self_uid)
        else:
            self._prepare_tables_future = asyncthread.future(
                lambda: self._prepare_tables(create_tables, readonly, proxy, ttl, with_self_uid)
            )

        if isinstance(max_cache_size, str):  # it's percentage
            self._max_cache_size = self._get_cache_size_from_percent(int(max_cache_size))
        else:
            self._max_cache_size = max_cache_size

        self._stager = kwargs.get("stager", utils.DummyStager())
        self._xx_client = None
        self._xx_prepare_data = False
        if kwargs.get('new_client'):
            try:
                import yalibrary.store.yt_store.xx_client as xx_client

                self._xx_client = xx_client.YtStoreWrapper(
                    proxy,
                    data_dir,
                    token,
                    retry_time_limit,
                )
                logger.debug('Will use new YT store client')
                self._xx_prepare_data = bool(kwargs.get('cpp_prepare_data'))
            except Exception:  # ModuleNotFoundError is py3-only :(
                logger.warning('Failed to init new yt store client', exc_info=True)
            if self._xx_prepare_data:
                logger.debug('Will use C++ reimplementation of prepare_data')

    @property
    def is_disabled(self):
        return self._client.is_disabled

    def _on_error_callback_for_retries_wrapper(self, err):
        pretty_error = repr(err)

        if self._yt_store_exclusive or self._heater_mode:
            logger.warning('Can\'t use dist cache: %s', pretty_error)
        else:
            if len(pretty_error) > 100:
                logger.warning(
                    "Disabling dist cache. Last caught error: %s...<Truncated. Complete message will be available in debug logs>",
                    pretty_error[:100],
                )
                logger.debug("Disabling dist cache. Last caught error: %s", pretty_error)
            else:
                logger.warning("Disabling dist cache. Last caught error: %s", pretty_error)

        labels = {
            "error_type": type(err).__name__,
            "yt_proxy": self._proxy,
            "yt_dir": self._data_dir,
            "is_heater": self._heater_mode,
        }

        try:
            import app_ctx

            if hasattr(app_ctx, 'metrics_reporter'):
                app_ctx.metrics_reporter.report_metric(
                    core_monitoring.MetricNames.YT_CACHE_ERROR,
                    labels=labels,
                    urgent=True,
                    report_type=report.ReportTypes.YT_CACHE_METRICS,
                )
        except Exception:
            logger.debug('Failed to report yt cache error metric to snowden', exc_info=True)
            pass

        labels['error'] = str(err)
        labels['user'] = core_config.get_user()

        report.telemetry.report(
            report.ReportTypes.YT_CACHE_ERROR,
            labels,
        )

    def _get_cache_size_from_percent(self, percentage):
        total = self._client.account_size_limit
        limit = total * percentage // 100
        logger.debug(
            'Account size total_bytes=%s, calculated_limit_bytes=%s, percentage=%s, total_human=%s, calculated_human=%s',
            total,
            limit,
            percentage,
            utils.human_size(total),
            utils.human_size(limit),
        )
        return limit

    def _prepare_tables(self, create_tables, readonly, proxy, ttl, with_self_uid):
        if create_tables and not readonly:
            try:
                self._client.create_tables(ttl, with_self_uid)
            except Exception as e:
                raise YtInitException('Can\'t create tables at {}: {}'.format(proxy, str(e)))

        try:
            self._client.assert_tables_are_ready(not self._retry_time_limit)
        except YtInitException:
            raise
        except Exception as e:
            raise YtInitException(e)

    def wait_until_tables_ready(self):
        if self._prepare_tables_future:
            self._prepare_tables_future()
            self._prepare_tables_future = None

    def _get_meta_to_delete(self, data_size_to_delete, min_access_time_threshold, limit):
        common_where = '(not is_null(access_time) and not is_null(data_size))'
        if data_size_to_delete > 0:
            # query with 'order by' is heavy so use it for data size limiting only
            meta = self._client.get_metadata_rows(where=common_where, order_by='access_time', limit=limit)
            meta_to_delete = []
            for row in meta:
                if data_size_to_delete > 0 or row['access_time'] < min_access_time_threshold:
                    meta_to_delete.append(row)
                    data_size_to_delete -= row['data_size']
                else:
                    break
        else:
            where = 'access_time < {} and {}'.format(min_access_time_threshold, common_where)
            meta_to_delete = self._client.get_metadata_rows(where=where, limit=limit)
        return meta_to_delete

    def prepare(self, self_uids, uids, refresh_on_read=False, content_uids=False, _async=False):
        if _async:
            self._prepare_future = asyncthread.future(
                lambda: self._load_meta(self_uids, uids, refresh_on_read, content_uids)
            )
        else:
            self._load_meta(self_uids, uids, refresh_on_read, content_uids)

    def _load_meta(self, self_uids, uids, refresh_on_read=False, content_uids=False):
        with self._stager.scope('loading-yt-meta'):
            self.wait_until_tables_ready()

            try:
                with AccumulateTime(lambda x: self._inc_time(x, 'get-meta')):
                    refresh_access_time = not self.readonly() and refresh_on_read
                    self._meta = self._client.get_metadata(
                        self_uids, uids, refresh_access_time=refresh_access_time, content_uids=content_uids
                    )
                    if self._time_to_first_recv_meta is None:
                        self._time_to_first_recv_meta = time.time()
                # logger.debug('YT cache has: %s', ', '.join(self._meta.keys()))
            except Exception as e:
                raise YtInitException('Can\'t read metadata at {}: {}'.format(self._proxy, str(e)))

            # v3 meta table can return more rows than uids were requested, since self_uids can be used as a row filter
            found_uids = set(uids).intersection(self._meta.keys())
            self._cache_hit = {'requested': len(uids), 'found': len(found_uids)}

            if self._max_cache_size and not self.readonly():
                size = self.get_used_size()
                if size > self._max_cache_size:
                    if self._heater_mode:
                        raise YtInitException(
                            "Cannot update yt store due to size, limit: {}, {}".format(size, self._max_cache_size)
                        )
                    logger.warning(
                        'Cache size (%s) exceeds limit of %s bytes, switch to readonly mode', size, self._max_cache_size
                    )
                    self._readonly = True

    def get_used_size(self):
        return self._client.get_tables_size()

    def get_max_size(self):
        return self._max_cache_size

    def _upload_data(self, stack, files, codec, root_dir, self_uid, uid, name, cuid):
        if codec == consts.YT_CACHE_NO_DATA_CODEC:
            raise Exception("Codec {} is not supported here".format(consts.YT_CACHE_NO_DATA_CODEC))
        data_path = self._prepare_data(stack, files, codec, root_dir)
        return self._client.put(self_uid, uid, data_path, name=name, codec=codec, cuid=cuid)

    def _prepare_data(self, stack, files, codec, root_dir):
        tar_path = stack.enter_context(tmp.temp_file())

        if self._xx_prepare_data:
            if codec == consts.YT_CACHE_NO_DATA_CODEC:
                codec = None
            raw_size = self._xx_client.prepare_data(tar_path, files, codec, root_dir)
            if codec:
                self._update_compression_ratio(raw_size, fs.get_file_size(tar_path))
            return tar_path

        tarfile_content = []
        for f in sorted(files):
            tarfile_content.append((os.path.abspath(f), os.path.relpath(f, root_dir)))
        archive.create_tar(tarfile_content, tar_path)

        if codec and codec != consts.YT_CACHE_NO_DATA_CODEC:
            data_path = stack.enter_context(tmp.temp_file())
            compress.compress(tar_path, data_path, codec=codec)
            self._update_compression_ratio(fs.get_file_size(data_path), fs.get_file_size(tar_path))
        else:
            data_path = tar_path
        return data_path

    def _probe_meta(self, self_uid, uid, files):
        if not self._probe_before_put:
            return False
        if self._probe_before_put_min_size:
            files_size = sum(os.lstat(f).st_size for f in files)
            if files_size < self._probe_before_put_min_size:
                return False

        with AccumulateTime(lambda x: self._inc_time(x, 'probe-meta-before-put')):
            meta = self._client.get_metadata(self_uids=[self_uid], uids=[uid])
        if not meta:
            return False
        self._counters['skip-put'] += 1
        # Report packed data size from YT store to be consistent with the put data_size
        self._inc_data_size(meta[uid].get('data_size', 0), 'skip-put')
        return True

    def _do_put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        if self.is_disabled:
            if self._heater_mode:
                raise Exception('Dist cache is disabled - no reason to work in heater_mode')
            else:
                return False

        name = files[0][len(root_dir) + 1 :] if len(files) else 'none'
        logger.debug('Put %s(%s) to YT', name, uid)

        if uid in self._meta or self.readonly():
            # should never happen
            logger.debug('Put %s(%s) to YT completed(no-op)', name, uid)
            return True

        try:
            if self._probe_meta(self_uid, uid, files):
                logger.debug('Put %s(%s) to YT completed(no-op): uid already exists', name, uid)
                return True
        except Exception:
            logger.debug("meta probing failed with error", exc_info=True)

        data_size = 0
        try:
            with ExitStack() as stack:
                meta = self._upload_data(stack, files, codec, root_dir, self_uid, uid, name, cuid)
                data_size = meta.get('data_size', 0)
                self._inc_data_size(data_size, 'put')
                self._meta[uid] = meta
        except Exception as e:
            logger.debug('Put %s(%s) to YT failed: %s', name, uid, e)
            self._count_failure('put')
            return False

        if data_size > 0:
            logger.debug('Put %s(%s) size=%d to YT completed', name, uid, data_size)
        else:
            logger.debug('Put %s(%s) to YT completed', name, uid)

        return True

    def put_stat(self, key, value, timestamp=None):
        timestamp = timestamp or utils.time_to_microseconds(time.time())
        self._client.put_stat(key, value, timestamp)

    def merge_stat(self):
        self._client.merge_stat_table()

    def touch(self, uid):
        pass

    def _do_has(self, uid):
        if self._time_to_first_call_has is None:
            self._time_to_first_call_has = time.time()

        if self._prepare_future:
            self._prepare_future(wrapped=True)
            self._prepare_future = None

        if self.is_disabled:
            return False

        _has = uid in self._meta
        logger.debug('YT Probing %s => %s', uid, _has)
        return _has

    def _do_try_restore(self, uid, into_dir, filter_func=None):
        if self.is_disabled:
            return False

        logger.debug('Try restore %s from YT', uid)

        if uid not in self._meta:
            # should never happen
            logger.debug('Try restore %s from YT failed: no metadata for uid', uid)
            self._count_failure('get')
            return False

        meta = self._meta[uid]

        if meta.get('codec') == consts.YT_CACHE_NO_DATA_CODEC:
            # yndexer hack, see documentation
            logger.debug("Can't restore data with service codec '{}'".format(consts.YT_CACHE_NO_DATA_CODEC))
            self._count_failure('get')
            return False

        if not meta.get('chunks_count') or not meta.get('hash'):
            # should never happen
            logger.debug('Try restore %s from YT failed: malformed metadata for uid', uid)
            self._count_failure('get')
            return False

        try:
            if self._xx_client is None or meta.get('data_size', None) is None:
                # data_size is required for c++ version but not for py (should never happend tho)
                with ExitStack() as stack:
                    data_path = stack.enter_context(tmp.temp_file())

                    self._client.get_data(meta['hash'], data_path, meta['chunks_count'])
                    stored_data_size = fs.get_file_size(data_path)
                    self._inc_data_size(stored_data_size, 'get')

                    if meta['codec']:
                        utils.check_cancel_state()
                        tar_path = stack.enter_context(tmp.temp_file())
                        compress.decompress(data_path, tar_path, codec=meta['codec'])
                        self._update_compression_ratio(stored_data_size, fs.get_file_size(tar_path))
                    else:
                        tar_path = data_path

                    utils.check_cancel_state()
                    archive.extract_from_tar(tar_path, into_dir)
            else:
                sz_decoded = self._xx_client.do_try_restore(
                    meta['hash'], into_dir, meta['codec'], meta['chunks_count'], meta['data_size']
                )
                # XXX now data_size is required field
                self._inc_data_size(meta['data_size'], 'get')
                if sz_decoded:
                    self._update_compression_ratio(sz_decoded, meta['data_size'])
        except Exception as e:
            # it is possible for xx_client module to not exist in common case (py2)
            if e.__class__.__name__ == 'NetworkException':
                self._client.retry_policy.on_error(e)
            logger.debug('Try restore %s from YT failed', uid, exc_info=True)
            self._count_failure('get')
            return False

        logger.debug('Try restore %s from YT completed. Successfully restored %s', uid, meta.get('name', ''))
        return True

    def flush(self):
        pass

    def analyze(self):
        pass

    def start_forced_compaction(self):
        if self.readonly():
            raise Exception("Compaction is not available in readonly mode")
        self._client.start_forced_compaction()

    def strip(self):
        if not (self._max_cache_size or self._ttl):
            logger.debug('Neither max_cache_size nor ttl are set. Nothing to strip')
            return

        if not self._client.is_table_format_v2:
            raise Exception('Current cache tables format doesn\'t support strip')

        counters = {'meta_rows': 0, 'data_rows': 0, 'data_size': 0}
        timers = {'read_meta': 0, 'delete_data': 0, 'get_replication_factor': 0}

        def inc_time(x, tag):
            timers[tag] += x

        with AccumulateTime(lambda x: inc_time(x, 'get_replication_factor')):
            replication_factor = self._client.get_data_replication_factor()
        with AccumulateTime(lambda x: inc_time(x, 'read_meta')):
            meta_status = self._client.get_metadata_status(precise_data_size=bool(self._max_cache_size))
        min_access_time = meta_status['min_access_time']
        total_data_size = meta_status['total_data_size'] * replication_factor

        min_access_time_threshold = 0
        max_cache_size = self._max_cache_size if self._max_cache_size else 0
        if self._ttl:
            min_access_time_threshold = utils.time_to_microseconds(time.time() - self._ttl * 3600)

        logger.debug(
            'Current cache status: net data_size: %d (%s), min access_time: %d (%s)',
            total_data_size,
            utils.human_size(total_data_size),
            min_access_time,
            utils.microseconds_to_str(min_access_time),
        )
        logger.debug(
            'Desired cache status: net data_size: %d (%s), min access_time: %d (%s)',
            max_cache_size,
            utils.human_size(max_cache_size),
            min_access_time_threshold,
            utils.microseconds_to_str(min_access_time_threshold),
        )
        logger.debug(
            'Overrun: data_size: %s, ttl: %s',
            utils.human_size(total_data_size - max_cache_size) if 0 < max_cache_size < total_data_size else '-',
            (
                utils.microsecond_interval_to_str(min_access_time_threshold - min_access_time)
                if min_access_time_threshold > min_access_time
                else '-'
            ),
        )

        if self.readonly():
            logger.debug('Do nothing in readonly mode')
            return

        while True:
            with AccumulateTime(lambda x: inc_time(x, 'read_meta')):
                net_size_to_delete = 0
                if total_data_size > max_cache_size > 0:
                    # Account data size with replication factor consideration but delete rows using data column net size
                    net_size_to_delete = (total_data_size - max_cache_size) // replication_factor
                meta_to_delete = self._get_meta_to_delete(
                    net_size_to_delete, min_access_time_threshold, consts.YT_CACHE_SELECT_METADATA_LIMIT
                )

            if meta_to_delete:
                with AccumulateTime(lambda x: inc_time(x, 'delete_data')):
                    deleted_data_row_count = self._client.delete_data(meta_to_delete)
                deleted_data_size = sum(m['data_size'] for m in meta_to_delete) * replication_factor
                total_data_size -= deleted_data_size
                counters['meta_rows'] += len(meta_to_delete)
                counters['data_rows'] += deleted_data_row_count
                counters['data_size'] += deleted_data_size

            if len(meta_to_delete) < consts.YT_CACHE_SELECT_METADATA_LIMIT:
                break

        logger.debug('Timings: %s', ', '.join('{}:{}'.format(tag, x) for tag, x in timers.items()))
        return counters

    def _update_compression_ratio(self, compressed_size, raw_size):
        self._total_compressed_size += compressed_size
        self._total_raw_size += raw_size

    @property
    def avg_compression_ratio(self):
        if self._total_raw_size:
            return float(self._total_compressed_size) / self._total_raw_size
        return 1.0

    def fits(self, node):
        if isinstance(node, dict):
            uid = node["uid"]
            outputs = node["outputs"]
            target_properties = node.get("target_properties") or {}
            kv = node.get("kv") or {}
        else:
            uid = node.uid
            outputs = node.outputs
            target_properties = node.target_properties
            kv = node.kv or {}

        if not len(outputs):
            return False

        if self._fits_filter:
            return self._fits_filter(node)

        if self._cache_filter is None:
            p = kv.get('p')
            if p in consts.YT_CACHE_EXCLUDED_P:
                return False
            for o in outputs:
                for p in 'library/cpp/svnversion', 'library/cpp/build_info':
                    if o.startswith('$(BUILD_ROOT)/' + p):
                        return False
            if all(o.endswith('.tar') for o in outputs):
                return False
            return True

        try:
            return eval(
                self._cache_filter,
                {'__builtins__': None},
                {
                    'is_module': lambda: 'module_type' in target_properties,
                    'outputs_to': lambda *paths: any(
                        any(o.startswith('$(BUILD_ROOT)/' + p) for p in paths) for o in outputs
                    ),
                    'kv': lambda k, v: k in kv if v is None else k in kv and kv[k] == v,
                    'module_type': lambda mt: mt == target_properties.get('module_type'),
                },
            )
        except Exception as e:
            logger.error('Can\'t evaluate YT cache filter for %s(%s): %s', uid, outputs[0], str(e))
            return False

    def get_status(self):
        '''Return basic cache status'''
        status = self._client.get_metadata_status()
        max_age = 0
        if status['min_access_time'] > 0:
            max_age = int(time.time() - status['min_access_time'] / 1000000)
        return {
            'data_size': status['total_data_size'],
            'max_age': max_age,
            'file_count': status['file_count'],
        }

    def stats(self, execution_log, evlog_writer):
        super(YtStore, self).stats(execution_log, evlog_writer)
        self._send_client_metrics()

    def _send_client_metrics(self):
        stat = {
            "time_to_first_recv_meta": self._time_to_first_recv_meta,
            "time_to_first_call_has": self._time_to_first_call_has,
            "failed_during_build": self.is_disabled,
            "failed_during_setup": not self._meta,
        }

        report.telemetry.report('{}-{}'.format(self._stats_name, 'additional-info'), stat)


class YndexerYtStore(YtStore):
    YDX_PB2_EXT = '.ydx.pb2.yt'

    def __init__(self, *args, **kwargs):
        super(YndexerYtStore, self).__init__(*args, **kwargs)
        if self._fits_filter is None:
            self._fits_filter = self.node_has_yt_upload_outputs

    @staticmethod
    def node_has_yt_upload_outputs(node):
        outputs = node["outputs"] if isinstance(node, dict) else node.outputs
        return any(out.endswith(YndexerYtStore.YDX_PB2_EXT) for out in outputs)

    def _upload_data(self, stack, files, codec, root_dir, self_uid, uid, name, cuid):
        if codec != consts.YT_CACHE_NO_DATA_CODEC:
            return super(YndexerYtStore, self)._upload_data(stack, files, codec, root_dir, self_uid, uid, name, cuid)
        forced_node_size = None

        size_file = list(filter(lambda x: x.endswith(self.YDX_PB2_EXT), files))
        if not size_file:
            logger.error('Sizefile not found. Real output size will be placed into dist cache.')
        elif len(size_file) > 1:
            logger.error('Too many sizefiles found. Real output size will be placed into dist cache.')
        else:
            try:
                with open(size_file[0]) as f:
                    stats = json.load(f)
                    forced_node_size = stats['bytes_to_upload']
            except Exception as e:
                logger.error(
                    'Can\'t read data size from sizefile: {}. Real output size will be placed into dist cache.'.format(
                        e
                    )
                )

        if forced_node_size is None:
            data_path = self._prepare_data(stack, files, codec, root_dir)
            forced_node_size = os.path.getsize(data_path)
        return self._client.put(
            self_uid, uid, None, name=name, codec=codec, forced_node_size=forced_node_size, cuid=cuid
        )
