from array import array
from typing import NamedTuple

from devtools.ya.yalibrary.runner.diagnostics import result_source as result_sources

from devtools.ya.build.stat.graph import _get_log
from devtools.ya.build.graph_description import DictGraph

_NO_TIME = -(1 << 63)
_CACHED = 1
_DYNAMIC = 2
_TEST = 4
_FAILED = 8
_SOURCE_SHIFT = 4
_SOURCE_MASK = 0x70
# Zero encodes an unobserved source; a finalized unknown backend needs a separate value.
_UNKNOWN_SOURCE = 6
_LEGACY_RUN = 128


class CacheStatistics(NamedTuple):
    legacy_counts: dict[str, int]
    cache_usage: result_sources.CacheUsage


def collect_cache_statistics(
    graph: DictGraph,
    distbuild_result: dict | None = None,
    local_result: dict | None = None,
    *,
    failed_uids: set[str] | None = None,
) -> CacheStatistics:
    """Collect cache counters without task objects, dependency edges or worker indices.

    Keep only a UID index, two packed timestamps and one flag byte per run UID.
    Timestamps are needed to match full statistics for repeated/out-of-order events
    and for local timings overriding remote timings. Copy/prepare events in the
    remote log are irrelevant to these counters and are not retained.
    """
    indices = {}
    starts = array('q')
    ends = array('q')
    # TODO(YMAKE-2453): remove mutable tracking once classic publishes terminal summaries.
    flags = bytearray()

    def task_index(uid):
        index = indices.get(uid)
        if index is None:
            index = len(indices)
            indices[uid] = index
            starts.append(_NO_TIME)
            ends.append(_NO_TIME)
            flags.append(0)
        return index

    def set_source(index: int, source: result_sources.ResultSource) -> None:
        flags[index] = (flags[index] & ~_SOURCE_MASK) | ((source or _UNKNOWN_SOURCE) << _SOURCE_SHIFT)

    complete = distbuild_result is not None
    if local_result:
        complete = (local_result.get(result_sources.LOG_MARKER) or {}).get(
            'schema_version'
        ) == result_sources.SCHEMA_VERSION

    if distbuild_result is not None:
        with _get_log(distbuild_result['log']) as log:
            for line in log:
                fields = line.rstrip().split(' ', 7)
                if len(fields) < 3:
                    continue
                event = fields[1]
                if event not in ('started', 'finished', 'finished_from_cache', 'deployed'):
                    continue
                timestamp = int(fields[0])
                index = task_index(fields[2])
                flags[index] |= _LEGACY_RUN
                if event in ('finished', 'finished_from_cache'):
                    if len(fields) > 4 and fields[4] == 'RS_SUCCESS':
                        set_source(
                            index,
                            (
                                result_sources.ResultSource.DISTBUILD_CACHE
                                if event == 'finished_from_cache'
                                else result_sources.ResultSource.EXECUTED
                            ),
                        )
                    else:
                        flags[index] &= ~_SOURCE_MASK
                        if len(fields) <= 4:
                            complete = False
                if event == 'finished_from_cache':
                    flags[index] |= _CACHED
                    starts[index] = ends[index] = timestamp
                elif event == 'finished':
                    ends[index] = timestamp
                else:
                    starts[index] = timestamp

    data_sizes = result_sources.data_sizes_from_log(
        local_result or {},
        distbuild_download_bytes=(
            0 if distbuild_result is not None and result_sources.LOG_MARKER in (local_result or {}) else None
        ),
    )
    # The same prepare key may be updated more than once; count its final type.
    prepare_types = {}
    restore_origins = {}
    for uid, info in (local_result or {}).items():
        if not info:
            continue
        if info.get('type') == 'download from DistBuild':
            data_sizes['distbuild_download_bytes'] = (data_sizes['distbuild_download_bytes'] or 0) + info['size']
        if 'result_source' in info:
            set_source(task_index(info.get('result_uid', uid)), info['result_source'])
        if info.get('prepare') is not None:
            key = '{}:{}'.format(info['prepare'], uid) if info['prepare'] else uid
            if info.get('type') is not None:
                prepare_types[key] = info['type']
            elif key not in prepare_types:
                prepare_types[key] = key
        elif info.get('timing') or info.get('cached') or info.get('larry_avoided'):
            index = task_index(uid)
            flags[index] |= _LEGACY_RUN
            flags[index] &= ~_DYNAMIC
            if info.get('dynamically_resolved_cache'):
                flags[index] |= _DYNAMIC
            if info.get('timing'):
                starts[index] = int(info['timing'][0] * 1000)
                ends[index] = int(info['timing'][1] * 1000)
            if info.get('cached'):
                restore_origins[uid] = info.get('larry_origin')
                flags[index] |= _CACHED
                starts[index] = ends[index] = _NO_TIME

    if distbuild_result is not None:
        for uid in distbuild_result.get('failed_results', {}).get('results', []):
            index = indices.get(uid)
            if index is not None:
                flags[index] |= _FAILED

    source_failures = set(failed_uids or ())
    if distbuild_result is not None:
        failures = distbuild_result.get('failed_results', {})
        source_failures.update(failures.get('results', ()))
        source_failures.update(failures.get('fails', {}))
    source_counts = dict.fromkeys(result_sources.SOURCE_NAMES.values(), 0)

    # Read only UID and the test marker. In particular, never traverse deps,
    # commands or materialize nodes from a compact graph into Python dicts.
    for node in graph['graph']:
        index = indices.get(node['uid'])
        if index is not None:
            source = (flags[index] & _SOURCE_MASK) >> _SOURCE_SHIFT
            if node['uid'] not in source_failures:
                if source == _UNKNOWN_SOURCE:
                    complete = False
                elif source:
                    source_counts[result_sources.SOURCE_NAMES[source]] += 1
            flags[index] &= ~_TEST
            if node.get('kv', {}).get('run_test_node', False):
                flags[index] |= _TEST

    counts = dict(
        all_run_tasks_count=0,
        dyn_cached_tasks_count=0,
        failed_task_count=0,
        not_cached_task_count=0,
        tests_task_count=0,
        local_cached_task_count=sum(t == 'get from local cache' for t in prepare_types.values())
        + sum(origin == 'local_cache' for origin in restore_origins.values()),
        dist_cached_task_count=sum(t == 'get from dist cache' for t in prepare_types.values())
        + sum(origin == 'yt_cache' for origin in restore_origins.values()),
    )
    for index, flag in enumerate(flags):
        if not flag & _LEGACY_RUN:
            continue
        counts['all_run_tasks_count'] += 1
        if flag & _DYNAMIC:
            counts['dyn_cached_tasks_count'] += 1
        if flag & _FAILED:
            counts['failed_task_count'] += 1
        if not flag & (_CACHED | _DYNAMIC) and starts[index] != _NO_TIME and ends[index] != _NO_TIME:
            if ends[index] != starts[index]:
                counts['not_cached_task_count'] += 1
                if flag & _TEST:
                    counts['tests_task_count'] += 1
    return CacheStatistics(
        counts,
        {
            **result_sources.make_cache_usage(
                source_counts,
                execution_mode='distbuild' if distbuild_result is not None else 'local',
                observations_complete=complete,
                read_allowed=result_sources.read_allowed_from_log(
                    local_result or {},
                    distbuild_cache=(
                        graph.get('conf', {}).get('cache', True) if distbuild_result is not None else None
                    ),
                ),
            ),
            'data_sizes': data_sizes,
        },
    )
