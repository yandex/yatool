import logging
import platform
import threading
import time
from functools import wraps

import six
from six.moves import xrange
from yt.wrapper import YtClient, TablePath
from yt.wrapper.common import update as yt_config_update
from yt.wrapper.default_config import retries_config, retry_backoff_config, get_config_from_env
from yt.wrapper.format import YsonFormat
from yt.yson import YsonList, get_bytes

import devtools.ya.core.gsid  # XXX
import yalibrary.store.yt_store.consts as consts
import yalibrary.store.yt_store.retries as retries
import yalibrary.store.yt_store.utils as utils
from exts import hashing
from exts.func import memoize

logger = logging.getLogger(__name__)


INSERT_ROWS_LIMIT = 75000  # Shouldn't be greater than 100k (YT limitation)


if six.PY3:
    from itertools import batched
else:
    import itertools

    def batched(iterable, n):
        if n < 1:
            raise ValueError('n must be at least one')
        iterator = iter(iterable)
        while True:
            batch = tuple(itertools.islice(iterator, n))
            if not batch:
                break
            yield batch


class YtStoreClient(object):
    NOT_LOADED_META_COLUMNS = {
        'tablet_hash',
        'hostname',
        'GSID',
    }

    def __init__(self, proxy, data_table, metadata_table, stat_table, token=None, retry_policy=None):
        self._proxy = proxy
        self._data_table = data_table
        self._metadata_table = metadata_table
        self._stat_table = stat_table
        self._token = token
        self._tls = threading.local()
        self._enable_proxy_cache()

        self.retry_policy = retry_policy or retries.RetryPolicy()

    @property
    @memoize()
    def table_format(self):
        if any(c['name'] == 'self_uid' for c in self._meta_schema):
            format = 'v3'
        elif any(c['name'] == 'access_time' for c in self._meta_schema):
            format = 'v2'
        else:
            format = 'legacy'

        logger.debug('Metadata table %s format: %s', self._metadata_table, format)

        return format

    @property
    def is_table_format_v2(self):
        return self.table_format in ('v2', 'v3')

    @property
    def is_table_format_v3(self):
        return self.table_format == 'v3'

    @property
    @memoize()
    def max_data_ttl_presents(self):
        attr_presents = self._client.exists('{}/@max_data_ttl'.format(self._metadata_table))
        if attr_presents:
            logger.debug('Metadata table %s has max_data_ttl attribute', self._metadata_table)
        return attr_presents

    @property
    @memoize()
    def account_size_limit(self):
        table_attrs = self._client.get('{}/@'.format(self._data_table))
        account_name = table_attrs['account']
        primary_medium = table_attrs['primary_medium']
        limits = self._client.get('//sys/accounts/{}/@resource_limits/disk_space_per_medium'.format(account_name))
        size_limit = limits[primary_medium]
        return size_limit

    @property
    def is_disabled(self):
        return self.retry_policy.disabled

    def exists(self, path):
        return self._client.exists(path)

    def assert_tables_are_ready(self):
        from yt.logger import LOGGER

        #  Use yt.wrapper because unwrapped client timeouts are hard to change (almost impossible)
        retries_policy = retries_config(
            count=consts.YT_CACHE_REQUEST_RETRIES,
            enable=True,
            backoff=retry_backoff_config(
                policy="constant_time",
                constant_time=100,  # ms
            ),
        )
        yt_fixed_config = {
            "proxy": {
                "request_timeout": consts.YT_CACHE_REQUEST_TIMEOUT_MS,
                "retries": retries_policy,
            },
            "dynamic_table_retries": retries_policy,
        }
        yt_config = yt_config_update(get_config_from_env(), yt_fixed_config)

        saved_log_level = LOGGER.getEffectiveLevel()
        try:
            # Suppress YT error messages during check
            LOGGER.setLevel(logging.CRITICAL)
            # Don't reuse self._client because we need special retry policy here
            ytc = YtClient(proxy=self._proxy, token=self._token, config=yt_config)
            for table in self._metadata_table, self._data_table:
                ytc.select_rows('1 from [{}] limit 1'.format(table), format="json")
            _ = self._meta_schema  # Init internal schema cache
        finally:
            LOGGER.setLevel(saved_log_level)

    @property
    @memoize()
    def _meta_schema(self):
        return self._client.get('{}/@schema'.format(self._metadata_table))

    @property
    def _client(self):
        if not hasattr(self._tls, 'yt_client'):
            client = retries.get_default_client(self._proxy, self._token)
            self._tls.yt_client = retries.YtClientProxy(client, self.retry_policy)
        return self._tls.yt_client

    def _create_table(self, table, attrs):
        if self._client.exists(table):
            logger.warning('Table %s already exists', table)
        else:
            self._client.create('table', table, attributes=attrs)

    def _create_stat_table(self):
        schema = YsonList(consts.YT_CACHE_STAT_SCHEMA)
        schema.attributes = {'strict': True}
        self._create_table(self._stat_table, {'schema': schema})

    def merge_stat_table(self):
        logger.debug("Merge table %s", self._stat_table)
        self._client.run_merge(TablePath(self._stat_table), TablePath(self._stat_table), spec={'combine_chunks': True})

    def create_dynamic_table(self, table, attrs):
        self._create_table(table, attrs)
        self._client.mount_table(table)

        while not all(t['state'] == 'mounted' for t in self._client.get('{}/@tablets'.format(table))):
            time.sleep(0.2)

    def create_tables(self, ttl, with_self_uid):
        attrs = {
            'auto_compaction_period': 24 * 60 * 60 * 1000,  # 1d
        }
        if ttl != 0:
            attrs['min_data_versions'] = 0
            attrs['max_data_ttl'] = ttl * 60 * 60 * 1000

        self.create_dynamic_table(self._data_table, utils.make_table_attrs(consts.YT_CACHE_DATA_SCHEMA, 256, **attrs))

        metadata_schema = consts.YT_CACHE_METADATA_V3_SCHEMA if with_self_uid else consts.YT_CACHE_METADATA_SCHEMA

        self.create_dynamic_table(self._metadata_table, utils.make_table_attrs(metadata_schema, 2**4, **attrs))

        self._create_stat_table()

    def get_tables_size(self):
        size = 0
        for table in self._data_table, self._metadata_table:
            size += self._client.get('{}/@resource_usage/disk_space'.format(table))
        return size

    def get_data_replication_factor(self):
        return self._client.get('{}/@replication_factor'.format(self._data_table))

    def put(self, self_uid, uid, path, codec=None, forced_node_size=None, **kwargs):
        chunks_count = 0
        data_size = 0
        _hash = hashing.fast_filehash(path) if path else ''  # TODO: streaming hash
        cur_timestamp_ms = utils.time_to_microseconds(time.time())

        if codec != consts.YT_CACHE_NO_DATA_CODEC:
            with open(path, "rb") as f:
                while True:
                    data = f.read(consts.YT_CACHE_RAM_PER_THREAD_LIMIT // 2)

                    if not data:
                        break

                    data_size += len(data)

                    rows = []
                    for begin in xrange(0, len(data), consts.YT_CACHE_CELL_LIMIT):
                        end = min(begin + consts.YT_CACHE_CELL_LIMIT, len(data))
                        row = {'hash': _hash, 'chunk_i': chunks_count, 'data': data[begin:end]}
                        if self.is_table_format_v2:
                            row['create_time'] = cur_timestamp_ms
                        rows.append(row)
                        chunks_count += 1
                    self._client.insert_rows(self._data_table, rows, atomicity='none', durability='async')
        else:
            data_size = forced_node_size or 0

        meta = {
            'uid': uid,
            'chunks_count': chunks_count,
            'hash': _hash,
            'name': kwargs.get('name'),
            'hostname': platform.node(),
            'GSID': devtools.ya.core.gsid.flat_session_id(),
            'codec': codec,
        }

        if self.is_table_format_v2:
            meta['access_time'] = cur_timestamp_ms
            meta['data_size'] = data_size

        if self.is_table_format_v3:
            meta['self_uid'] = self_uid

        self._client.insert_rows(self._metadata_table, [meta], atomicity='none', durability='async')

        cuid = kwargs.get('cuid')
        if self.is_table_format_v3 and cuid and cuid != uid:
            meta['uid'] = cuid
            self._client.insert_rows(self._metadata_table, [meta], atomicity='none', durability='async')

        return meta

    def get_metadata_status(self):
        query = '{}, {} from [{}] group by 1'.format(
            'min(access_time) as min_access_time',
            'sum(1) as file_count',
            self._metadata_table,
        )
        rows = list(
            self._client.select_rows(
                query,
                input_row_limit=consts.YT_CACHE_SELECT_INPUT_ROW_LIMIT,
                output_row_limit=consts.YT_CACHE_SELECT_OUTPUT_ROW_LIMIT,
            )
        )
        if len(rows) == 0:
            return {'min_access_time': 0, 'total_data_size': 0, 'file_count': 0}
        if len(rows) > 1:
            # should never happen
            raise Exception('Query should return single row')
        stat = rows[0]
        total_data_size = 0
        # Get exact data size (slow but precise)
        rows = self._client.select_rows(
            'first(data_size) as ds from [{}] where data_size is not null group by hash'.format(self._metadata_table),
            input_row_limit=consts.YT_CACHE_SELECT_INPUT_ROW_LIMIT,
            output_row_limit=consts.YT_CACHE_SELECT_OUTPUT_ROW_LIMIT,
        )
        for row in rows:
            total_data_size += row['ds']
        stat['total_data_size'] = total_data_size
        return stat

    def get_metadata_rows(self, self_uids=None, uids=None, where=None, order_by=None, limit=None, content_uids=False):
        if self.is_table_format_v3 and content_uids:
            assert self_uids

            query = '* from [{}] where self_uid in ({})'.format(
                self._metadata_table,
                ','.join('"{}"'.format(self_uid) for self_uid in self_uids),
            )

            rows = list(
                self._client.select_rows(
                    query,
                    input_row_limit=consts.YT_CACHE_SELECT_INPUT_ROW_LIMIT,
                    output_row_limit=consts.YT_CACHE_SELECT_OUTPUT_ROW_LIMIT,
                )
            )
        elif uids:
            columns = [col['name'] for col in self._meta_schema if col['name'] not in self.NOT_LOADED_META_COLUMNS]

            if self.is_table_format_v3:
                assert len(self_uids) == len(uids)

                keys = [{'self_uid': self_uid, 'uid': uid} for (self_uid, uid) in zip(self_uids, uids)]
            else:
                keys = [{'uid': uid} for uid in uids]

            rows = list(
                self._client.lookup_rows(
                    self._metadata_table,
                    keys,
                    column_names=tuple(map(six.ensure_binary, columns)),
                )
            )
        else:
            query = '* from [{}]'.format(self._metadata_table)
            if where:
                query += ' where {}'.format(where)
            if order_by:
                query += ' order by {}'.format(order_by)
            if limit:
                query += ' limit {}'.format(limit)
            rows = list(
                self._client.select_rows(
                    query,
                    input_row_limit=consts.YT_CACHE_SELECT_INPUT_ROW_LIMIT,
                    output_row_limit=consts.YT_CACHE_SELECT_OUTPUT_ROW_LIMIT,
                )
            )
        logger.debug("Fetched %d metadata rows from YT", len(rows))
        return rows

    def get_metadata(self, self_uids=None, uids=None, refresh_access_time=False, content_uids=False):
        meta = {}
        rows = self.get_metadata_rows(self_uids=self_uids, uids=uids, content_uids=content_uids)

        for row in rows:
            meta[row['uid']] = row

        if refresh_access_time and not self.max_data_ttl_presents:
            if self.is_table_format_v3 and content_uids:
                # Avoid infinite cache growth
                uids_set = set(uids)
                rows = [row for row in rows if row['uid'] in uids_set]

            self.refresh_access_time(rows)

        return meta

    def _lookup_rows_with_retry(self, keys):
        return list(self._client.lookup_rows(self._data_table, keys, format=YsonFormat(format="binary")))

    def get_data(self, hash, path, chunks_count):
        chunks_per_query = consts.YT_CACHE_RAM_PER_THREAD_LIMIT // consts.YT_CACHE_CELL_LIMIT

        with open(path, 'wb') as f:
            for begin in xrange(0, chunks_count, chunks_per_query):
                utils.check_cancel_state()
                end = min(begin + chunks_per_query, chunks_count)
                keys = [{'hash': hash, 'chunk_i': i} for i in xrange(begin, end)]

                rows = self._lookup_rows_with_retry(keys)

                if len(rows) < len(keys):
                    raise Exception('Can\'t read {} data: missing chunk from {} to {}'.format(hash, begin, end))

                for row in rows:
                    # We get raw binary data from YT, so we don't want to convert it into string and slow down just for write it info file
                    f.write(get_bytes(row['data']))

        real_hash = hashing.fast_filehash(path)  # TODO: streaming hash
        if hash != real_hash:
            raise Exception('Can\'t read {} data: wrong hash {}'.format(hash, real_hash))

    def refresh_access_time(self, meta_rows):
        if meta_rows:
            cur_timestamp_ms = utils.time_to_microseconds(time.time())

            def make_row(r):
                u = {'uid': r['uid'], 'access_time': cur_timestamp_ms}
                if self.is_table_format_v3:
                    u['self_uid'] = r['self_uid']
                return u

            rows = [make_row(r) for r in meta_rows]
            for batch in batched(rows, INSERT_ROWS_LIMIT):
                self._client.insert_rows(self._metadata_table, batch, update=True, atomicity='none', durability='async')
            logger.debug("Update access_time in %d metadata rows", len(rows))

    def get_hashes_to_delete(self, hashes_to_check):
        hashes_in_store = set()
        rows = self.get_metadata_rows(
            where="hash in ({})".format(", ".join(["'{}'".format(x) for x in hashes_to_check]))
        )

        for row in rows:
            hashes_in_store.add(row['hash'])

        return hashes_to_check - hashes_in_store

    def delete_data(self, meta_to_delete):
        data_keys = []
        hashes_to_check = set()
        chunks = {}
        for meta in meta_to_delete:
            chunks[meta['hash']] = meta['chunks_count']
            hashes_to_check.add(meta['hash'])

        if self.is_table_format_v3:
            meta_keys = [{'self_uid': m['self_uid'], 'uid': m['uid']} for m in meta_to_delete]
        else:
            meta_keys = [{'uid': m['uid']} for m in meta_to_delete]

        self._client.delete_rows(self._metadata_table, meta_keys, atomicity='none', durability='async')

        for content_hash in self.get_hashes_to_delete(hashes_to_check):
            rows = [{'hash': content_hash, 'chunk_i': i} for i in xrange(chunks[content_hash])]
            data_keys += rows

        data_row_count = len(data_keys)
        self._client.delete_rows(self._data_table, data_keys, atomicity='none', durability='async')
        return data_row_count

    def start_forced_compaction(self):
        for table in self._data_table, self._metadata_table:
            logger.debug("Start forced compaction of %s", table)
            self._client.set_attribute(table, "forced_compaction_revision", 1)
            self._client.remount_table(table)

    def put_stat(self, key, value, timestamp):
        if not self._client.exists(self._stat_table):
            logger.warning(
                'Cannot put stat because table %s doesn\'t exist. Create the missed table with "ya m -j0 --yt-create-tables -yt-put"',
                self._stat_table,
            )
            return
        rows = ({'timestamp': timestamp, 'key': key, 'value': value},)
        table_path = TablePath(self._stat_table, append=True)
        self._client.write_table(table_path, rows, format=YsonFormat(format='text', require_yson_bindings=False))

    @staticmethod
    def _cache_proxy(func):
        PROXY_REFRESH_INTERVAL = 10

        @wraps(func)
        def wrapper(provider):
            attr_name = '__cached_{}'.format(func.__name__)
            proxy_info = getattr(provider.client, attr_name, None)
            if proxy_info is None or proxy_info[1] + PROXY_REFRESH_INTERVAL < time.time():
                proxies = func(provider)
                proxy_info = (proxies, time.time())
                setattr(provider.client, attr_name, proxy_info)
            return proxy_info[0]

        return wrapper

    def _enable_proxy_cache(self):
        import yt.wrapper.http_driver

        yt.wrapper.http_driver.HeavyProxyProvider._discover_heavy_proxies = self._cache_proxy(
            yt.wrapper.http_driver.HeavyProxyProvider._discover_heavy_proxies
        )
        yt.wrapper.http_driver.HeavyProxyProvider._get_light_proxy = self._cache_proxy(
            yt.wrapper.http_driver.HeavyProxyProvider._get_light_proxy
        )
