"""Share final-result sources without redefining historical cache task counters.

Metadata hits and legacy restore labels include outcomes that did not produce a
usable result. Executors publish a source only after successful finalization.
"""

import enum
from collections.abc import Mapping
from typing import Literal, Protocol, TypedDict

SCHEMA_VERSION = 1
LOG_MARKER = 'result_source_schema'


class ResultSource(enum.IntEnum):
    UNKNOWN = 0
    LOCAL = 1
    YT = 2
    BAZEL = 3
    DISTBUILD_CACHE = 4
    EXECUTED = 5


SOURCE_NAMES = {
    ResultSource.LOCAL: 'local',
    ResultSource.YT: 'yt',
    ResultSource.BAZEL: 'bazel',
    ResultSource.DISTBUILD_CACHE: 'distbuild_cache',
    ResultSource.EXECUTED: 'executed',
}


_STORE_SOURCES = {'YT': ResultSource.YT, 'BAZEL': ResultSource.BAZEL}


class ResultNodeCounts(TypedDict):
    # Final graph results restored from the local artifact cache.
    local: int
    # Final graph results restored from the YT artifact cache.
    yt: int
    # Final graph results restored from the Bazel artifact cache.
    bazel: int
    # Final graph results supplied by the DistBuild worker cache.
    distbuild_cache: int
    # Graph nodes whose commands and result finalization succeeded.
    executed: int


class CacheReadPermissions(TypedDict):
    # Permission to restore local artifacts; None means the policy was not recorded.
    local: bool | None
    # Permission to restore YT artifacts, independent of metadata reads or backend health.
    yt: bool | None
    # Permission to restore Bazel artifacts through the selected cache composition.
    bazel: bool | None
    # Worker-cache permission from the submitted DistBuild graph's conf.cache.
    distbuild_cache: bool | None


class CacheDataSizes(TypedDict):
    # Larry's restored regular-file bytes, including service metadata.
    local_restored_file_bytes: int | None
    # Larry's restored YT files before finalization, not packed or network bytes.
    yt_restored_file_bytes: int | None
    # Larry's restored Bazel files before finalization, not network bytes.
    bazel_restored_file_bytes: int | None
    # Classic YT's successful GET blob bytes, compressed when a codec is enabled.
    yt_get_blob_bytes: int | None
    # Classic YT's successful PUT blob bytes, excluding metadata-only reuse.
    yt_put_blob_bytes: int | None
    # Classic YT's probe-before-PUT blob volume skipped without uploading it.
    yt_skipped_put_blob_bytes: int | None
    # Classic Bazel's full uncompressed GET manifest size, not transferred bytes.
    bazel_get_file_bytes: int | None
    # Classic Bazel's full uncompressed PUT manifest size after successful upload.
    bazel_put_file_bytes: int | None
    # Successful client-side DistBuild downloads; failed attempts are not included.
    distbuild_download_bytes: int | None


class CacheUsage(TypedDict):
    # Version of this aggregate contract, separate from legacy cache-hit statistics.
    schema_version: int
    # Runner used for this invocation; keeps classic and Larry cohorts separate.
    executor: Literal['classic', 'larry']
    # Actual execution path, including adaptive local execution instead of DistBuild.
    execution_mode: Literal['local', 'distbuild']
    # Scope of result_nodes; client-reported data_sizes have separate backend semantics.
    counting_scope: Literal['successful_final_results']
    # Completeness of source observations, independent of overall build success.
    observations_complete: bool
    # One mutually exclusive source per successfully finalized original graph UID.
    result_nodes: ResultNodeCounts
    # Dispatch-time restore permissions, not availability or per-node eligibility.
    read_allowed: CacheReadPermissions
    # Existing client measurements in bytes; None means no measurement was provided.
    data_sizes: CacheDataSizes


class TaggedStore(Protocol):
    def tag(self) -> str: ...


def source_from_store(store: TaggedStore) -> ResultSource:
    return _STORE_SOURCES.get(store.tag(), ResultSource.UNKNOWN)


def classic_read_allowed(
    store: TaggedStore | None, *, clear_build: bool, use_distbuild: bool = False
) -> CacheReadPermissions:
    """Describe selected artifact readers, independently of write modes or later failures."""
    # The DistBuild client downloads results directly, bypassing artifact restore tasks.
    enabled = not clear_build and not use_distbuild
    remote = source_from_store(store) if store is not None and enabled else ResultSource.UNKNOWN
    return {
        'local': enabled,
        'yt': enabled and remote is ResultSource.YT,
        'bazel': enabled and remote is ResultSource.BAZEL,
        'distbuild_cache': False,
    }


def read_allowed_from_log(
    execution_log: Mapping[str, dict], *, distbuild_cache: bool | None = None
) -> dict[str, bool | None] | None:
    """Keep old policies unknown and prefer the submitted graph for DistBuild reads."""
    policy = (execution_log.get(LOG_MARKER) or {}).get('read_allowed')
    if distbuild_cache is not None:
        return {**(policy or {}), 'distbuild_cache': distbuild_cache}
    return dict(policy) if policy is not None else None


def data_sizes_from_log(
    execution_log: Mapping[str, dict], *, distbuild_download_bytes: int | None = None
) -> CacheDataSizes:
    """Keep client-reported units instead of inventing a common byte hit rate."""

    def size(key: str) -> int | None:
        return (execution_log.get(key) or {}).get('data_size')

    return {
        'local_restored_file_bytes': None,
        'yt_restored_file_bytes': None,
        'bazel_restored_file_bytes': None,
        'yt_get_blob_bytes': size('$(yt-store-get-data-size)'),
        'yt_put_blob_bytes': size('$(yt-store-put-data-size)'),
        'yt_skipped_put_blob_bytes': size('$(yt-store-skip-put-data-size)'),
        'bazel_get_file_bytes': size('$(bazel-store-get-data-size)'),
        'bazel_put_file_bytes': size('$(bazel-store-put-data-size)'),
        'distbuild_download_bytes': distbuild_download_bytes,
    }


def make_cache_usage(
    counts: Mapping[str, int],
    *,
    executor: Literal['classic', 'larry'] = 'classic',
    execution_mode: Literal['local', 'distbuild'] = 'local',
    observations_complete: bool = True,
    read_allowed: Mapping[str, bool | None] | None = None,
) -> CacheUsage:
    reads = read_allowed or {}
    return {
        'schema_version': SCHEMA_VERSION,
        'executor': executor,
        'execution_mode': execution_mode,
        'counting_scope': 'successful_final_results',
        'observations_complete': observations_complete,
        'data_sizes': data_sizes_from_log({}),
        'read_allowed': {
            'local': reads.get('local'),
            'yt': reads.get('yt'),
            'bazel': reads.get('bazel'),
            'distbuild_cache': reads.get('distbuild_cache'),
        },
        'result_nodes': {
            'local': counts.get('local', 0),
            'yt': counts.get('yt', 0),
            'bazel': counts.get('bazel', 0),
            'distbuild_cache': counts.get('distbuild_cache', 0),
            'executed': counts.get('executed', 0),
        },
    }
