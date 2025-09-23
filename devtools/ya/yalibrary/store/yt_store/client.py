import logging
import platform
import random
import threading
import time
from functools import wraps
from itertools import batched

from six.moves import xrange
from yt.wrapper import YtClient, TablePath
from yt.wrapper.common import update as yt_config_update
from yt.wrapper.errors import YtTabletTransactionLockConflict
from yt.wrapper.default_config import retries_config, retry_backoff_config, get_config_from_env
from yt.wrapper.format import YsonFormat
from yt.yson import get_bytes

import devtools.ya.core.gsid  # XXX
import yalibrary.store.yt_store.consts as consts
import yalibrary.store.yt_store.retries as retries
import yalibrary.store.yt_store.utils as utils
from exts import hashing
from exts.func import memoize

logger = logging.getLogger(__name__)


INSERT_ROWS_LIMIT = 5000  # Too high value increases transaction conflicts
METADATA_REFRESH_AGE_THRESHOLD_SEC = 300
TRANSACTION_CONFLICT_SLEEP_INTERVAL = (0.05, 0.15)


def _sleep_after_transaction_conflict():
    time.sleep(random.uniform(*TRANSACTION_CONFLICT_SLEEP_INTERVAL))


class YtStoreClient(object):
    NOT_LOADED_META_COLUMNS = {
        'tablet_hash',
        'hostname',
        'GSID',
    }

    def __init__(
        self, proxy, data_table, metadata_table, stat_table, token=None, retry_policy=None, sync_transaction=None
    ):
        self._proxy = proxy
        self._data_table = data_table
        self._metadata_table = metadata_table
        self._stat_table = stat_table
        self._token = token
        self._tls = threading.local()
        self._enable_proxy_cache()
        self._durability = 'sync' if sync_transaction else 'async'
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
        table_attrs = self._client.get(
            '{}/@'.format(self._data_table), read_from='cache', attributes=['account', 'primary_medium']
        )
        account_name = table_attrs['account']
        primary_medium = table_attrs['primary_medium']
        limits = self._client.get(
            '//sys/accounts/{}/@resource_limits/disk_space_per_medium'.format(account_name), read_from='cache'
        )
        size_limit = limits[primary_medium]
        return size_limit

    @property
    def is_disabled(self):
        return self.retry_policy.disabled

    def exists(self, path):
        return self._client.exists(path)

    def assert_tables_are_ready(self, fast_check=True):
        from yt.logger import LOGGER

        saved_log_level = LOGGER.getEffectiveLevel()
        try:
            # Suppress YT error messages during check
            LOGGER.setLevel(logging.CRITICAL)
            ytc = self._fast_check_client() if fast_check else self._client
            try:
                for table in self._metadata_table, self._data_table:
                    ytc.select_rows('1 from [{}] limit 1'.format(table), format="json")
            except Exception as e:
                if fast_check:
                    # TODO YA-2591, issue #1
                    # We don't use retry_policy in the fast checking so we should explicitly notify it about an error
                    self.retry_policy.on_error(e)
                raise
            _ = self._meta_schema  # Init internal schema cache

        finally:
            LOGGER.setLevel(saved_log_level)

    def _fast_check_client(self):
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
        return YtClient(proxy=self._proxy, token=self._token, config=yt_config)

    @property
    @memoize()
    def _meta_schema(self):
        return self._client.get('{}/@schema'.format(self._metadata_table), read_from='cache')

    def _atomicity(self):
        return self._client.get('{}/@atomicity'.format(self._metadata_table), read_from='cache')

    @property
    @memoize()
    def _integrity(self):
        return {'atomicity': 'none', 'durability': self._durability} if self._atomicity() == 'none' else {}

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

    def merge_stat_table(self):
        logger.debug("Merge table %s", self._stat_table)
        if self._client.get('{}/@dynamic'.format(self._stat_table)):
            logger.info("There is no need to merge the dynamic table: %s", self._stat_table)
            return
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
        if ttl:
            attrs['min_data_versions'] = 0
            attrs['max_data_ttl'] = ttl * 60 * 60 * 1000

        self.create_dynamic_table(self._data_table, utils.make_table_attrs(consts.YT_CACHE_DATA_SCHEMA, 256, **attrs))

        metadata_schema = consts.YT_CACHE_METADATA_V3_SCHEMA if with_self_uid else consts.YT_CACHE_METADATA_SCHEMA

        self.create_dynamic_table(self._metadata_table, utils.make_table_attrs(metadata_schema, 2**4, **attrs))
        stat_attrs = attrs | {"enable_dynamic_store_read": True}
        self.create_dynamic_table(
            self._stat_table, utils.make_table_attrs(consts.YT_CACHE_STAT_SCHEMA, 1, **stat_attrs)
        )

    def get_tables_size(self):
        size = 0
        for table in self._data_table, self._metadata_table:
            size += self._client.get('{}/@resource_usage/disk_space'.format(table), read_from='cache')
        return size

    def get_data_replication_factor(self):
        return self._client.get('{}/@replication_factor'.format(self._data_table))

    def put(self, self_uid, uid, path, codec=None, forced_node_size=None, forced_hash='', cuid=None, name=None):
        chunks_count = 0
        data_size = 0
        _hash = hashing.fast_filehash(path) if path else forced_hash  # TODO: streaming hash
        cur_timestamp_ms = utils.time_to_microseconds(time.time())

        if codec != consts.YT_CACHE_NO_DATA_CODEC and path is not None:
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
                    self._safe_insert_rows(self._data_table, rows, ['hash', 'chunk_i'])
        else:
            data_size = forced_node_size or 0

        key_columns = ['uid']
        meta = {
            'uid': uid,
            'chunks_count': chunks_count,
            'hash': _hash,
            'name': name,
            'hostname': platform.node(),
            'GSID': devtools.ya.core.gsid.flat_session_id(),
            'codec': codec,
        }

        if self.is_table_format_v2:
            meta['access_time'] = cur_timestamp_ms
            meta['data_size'] = data_size

        if self.is_table_format_v3:
            key_columns = ['self_uid', 'uid']
            meta['self_uid'] = self_uid
            meta['cuid'] = cuid if cuid and cuid != uid else ''
            meta['create_time'] = cur_timestamp_ms

        self._safe_insert_rows(self._metadata_table, [meta], key_columns)

        return meta

    def get_metadata_status(self, precise_data_size=False):
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

        if precise_data_size:
            # Get exact data size (slow but precise)
            query = f'first(data_size) as ds from [{self._metadata_table}] where data_size is not null group by hash'
        else:
            query = f'sum(data_size) as ds from [{self._metadata_table}] where data_size is not null group by 1'

        rows = self._client.select_rows(
            query,
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
            columns = ",".join(
                col['name'] for col in self._meta_schema if col['name'] not in self.NOT_LOADED_META_COLUMNS
            )

            query = '{} from [{}] where self_uid in ({})'.format(
                columns,
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
                    column_names=columns,
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
            if cuid := row.get('cuid'):
                meta[cuid] = row

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
        if not meta_rows:
            return

        if self.is_table_format_v3:
            key_columns = ['self_uid', 'uid']
        else:
            key_columns = ['uid']
        lookup_columns = key_columns + ['access_time']

        def make_key(r):
            return {k: r[k] for k in key_columns}

        def make_row(r, ts):
            u = make_key(r)
            u['access_time'] = ts
            return u

        cur_time = time.time()
        cur_timestamp_us = utils.time_to_microseconds(cur_time)
        threshold_timestamp_us = utils.time_to_microseconds(cur_time - METADATA_REFRESH_AGE_THRESHOLD_SEC)
        updated_row_count = 0
        conflict_count = 0
        for batch in batched(meta_rows, INSERT_ROWS_LIMIT):
            while True:
                rows = [make_row(r, cur_timestamp_us) for r in batch if r['access_time'] < threshold_timestamp_us]
                if not rows:
                    break
                try:
                    self._client.insert_rows(self._metadata_table, rows, update=True, **self._integrity)
                    updated_row_count += len(rows)
                    break
                except YtTabletTransactionLockConflict:
                    conflict_count += 1
                    _sleep_after_transaction_conflict()
                batch = self._client.lookup_rows(
                    self._metadata_table,
                    [make_key(r) for r in rows],
                    column_names=lookup_columns,
                )
        logger.debug(
            "Update access_time in %d metadata rows with %d transaction conflicts", updated_row_count, conflict_count
        )

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

        self._client.delete_rows(self._metadata_table, meta_keys, **self._integrity)

        for content_hash in self.get_hashes_to_delete(hashes_to_check):
            rows = [{'hash': content_hash, 'chunk_i': i} for i in xrange(chunks[content_hash])]
            data_keys += rows

        data_row_count = len(data_keys)
        self._client.delete_rows(self._data_table, data_keys, **self._integrity)
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
        dynamic = self._client.get(self._stat_table + "/@dynamic")
        row = {'timestamp': timestamp, 'key': key, 'value': value}
        if dynamic:
            row['salt'] = random.randrange(1 << 64)
            self._client.insert_rows(self._stat_table, [row], **self._integrity)
        else:
            table_path = TablePath(self._stat_table, append=True)
            self._client.write_table(table_path, [row], format=YsonFormat(format='text', require_yson_bindings=False))

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

    def _safe_insert_rows(self, table, rows, key_columns):
        """Insert and skip existing rows on transaction conflict"""
        while rows:
            try:
                self._client.insert_rows(table, rows, **self._integrity)
                return
            except YtTabletTransactionLockConflict:
                _sleep_after_transaction_conflict()
            keys = [{k: r[k] for k in key_columns} for r in rows]
            existing_rows = self._client.lookup_rows(table, keys, column_names=key_columns)
            already_inserted = set()
            for r in existing_rows:
                already_inserted.add(tuple(r[k] for k in key_columns))
            rows = [r for r in rows if tuple(r[k] for k in key_columns) not in already_inserted]
