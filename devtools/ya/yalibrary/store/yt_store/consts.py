YT_CACHE_CELL_LIMIT = 14 * 2**20
YT_CACHE_HASH_SPACE = 2**20
YT_CACHE_RAM_PER_THREAD_LIMIT = 64 * 2**20
YT_CACHE_SELECT_METADATA_LIMIT = 10000
YT_CACHE_SELECT_INPUT_ROW_LIMIT = 4000000000  # Treat the value as 'unlimited'
YT_CACHE_SELECT_OUTPUT_ROW_LIMIT = 4000000000  # Treat the value as 'unlimited'
YT_CACHE_EXCLUDED_P = frozenset(['UN', 'PK', 'GO', 'ld', 'SB', 'CP', 'DL', 'TS_DEP'])
# Used in dynamic tables readiness check only
YT_CACHE_REQUEST_TIMEOUT_MS = 5000
YT_CACHE_REQUEST_RETRIES = 2

YT_CACHE_NO_DATA_CODEC = "no_data"

YT_CACHE_DEFAULT_DATA_TABLE_NAME = "data"
YT_CACHE_DEFAULT_METADATA_TABLE_NAME = "metadata"
YT_CACHE_DEFAULT_STAT_TABLE_NAME = "stat"


YT_CACHE_METADATA_SCHEMA = [
    {
        'name': 'tablet_hash',
        'expression': 'farm_hash(uid) % {}'.format(YT_CACHE_HASH_SPACE),
        'type': 'uint64',
        'sort_order': 'ascending',
    },
    {'name': 'uid', 'type': 'string', 'sort_order': 'ascending'},
    {'name': 'chunks_count', 'type': 'uint64'},
    {'name': 'data_size', 'type': 'uint64'},
    {'name': 'access_time', 'type': 'timestamp'},
    {'name': 'hash', 'type': 'string'},
    {'name': 'name', 'type': 'string'},
    {'name': 'hostname', 'type': 'string'},
    {'name': 'GSID', 'type': 'string'},
    {'name': 'codec', 'type': 'string'},
]

YT_CACHE_METADATA_V3_SCHEMA = [
    {
        'name': 'tablet_hash',
        'expression': 'farm_hash(uid) % {}'.format(YT_CACHE_HASH_SPACE),
        'type': 'uint64',
        'sort_order': 'ascending',
    },
    {'name': 'self_uid', 'type': 'string', 'sort_order': 'ascending'},
    {'name': 'uid', 'type': 'string', 'sort_order': 'ascending'},
    {'name': 'chunks_count', 'type': 'uint64'},
    {'name': 'data_size', 'type': 'uint64'},
    {'name': 'create_time', 'type': 'timestamp'},
    {'name': 'access_time', 'type': 'timestamp'},
    {'name': 'hash', 'type': 'string'},
    {'name': 'name', 'type': 'string'},
    {'name': 'hostname', 'type': 'string'},
    {'name': 'GSID', 'type': 'string'},
    {'name': 'codec', 'type': 'string'},
]

YT_CACHE_DATA_SCHEMA = [
    {
        'name': 'tablet_hash',
        'expression': 'farm_hash(hash) % {}'.format(YT_CACHE_HASH_SPACE),
        'type': 'uint64',
        'sort_order': 'ascending',
    },
    {'name': 'hash', 'type': 'string', 'sort_order': 'ascending'},
    {'name': 'chunk_i', 'type': 'uint64', 'sort_order': 'ascending'},
    {'name': 'create_time', 'type': 'timestamp'},
    {'name': 'data', 'type': 'string'},
]

YT_CACHE_STAT_SCHEMA = [
    {'name': 'timestamp', 'type_v3': 'timestamp'},
    {'name': 'key', 'type_v3': 'string'},
    {'name': 'value', 'type_v3': {'type_name': 'optional', 'item': 'yson'}},
]
