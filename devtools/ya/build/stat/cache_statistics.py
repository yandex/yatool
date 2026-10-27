from array import array

from devtools.ya.build.stat.graph import _get_log

_NO_TIME = -(1 << 63)
_CACHED = 1
_DYNAMIC = 2
_TEST = 4
_FAILED = 8


def collect_cache_statistics(graph, distbuild_result=None, local_result=None):
    """Collect cache counters without task objects, dependency edges or worker indices.

    Keep only a UID index, two packed timestamps and one flag byte per run UID.
    Timestamps are needed to match full statistics for repeated/out-of-order events
    and for local timings overriding remote timings. Copy/prepare events in the
    remote log are irrelevant to these counters and are not retained.
    """
    indices = {}
    starts = array('q')
    ends = array('q')
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

    if distbuild_result is not None:
        with _get_log(distbuild_result['log']) as log:
            for line in log:
                fields = line.rstrip().split(' ', 3)
                if len(fields) < 3:
                    continue
                event = fields[1]
                if event not in ('started', 'finished', 'finished_from_cache', 'deployed'):
                    continue
                timestamp = int(fields[0])
                index = task_index(fields[2])
                if event == 'finished_from_cache':
                    flags[index] |= _CACHED
                    starts[index] = ends[index] = timestamp
                elif event == 'finished':
                    ends[index] = timestamp
                else:
                    starts[index] = timestamp

    # The same prepare key may be updated more than once; count its final type.
    prepare_types = {}
    for uid, info in (local_result or {}).items():
        if not info:
            continue
        if info.get('prepare') is not None:
            key = '{}:{}'.format(info['prepare'], uid) if info['prepare'] else uid
            if info.get('type') is not None:
                prepare_types[key] = info['type']
            elif key not in prepare_types:
                prepare_types[key] = key
        elif info.get('timing') or info.get('cached'):
            index = task_index(uid)
            flags[index] &= ~_DYNAMIC
            if info.get('dynamically_resolved_cache'):
                flags[index] |= _DYNAMIC
            if info.get('timing'):
                starts[index] = int(info['timing'][0] * 1000)
                ends[index] = int(info['timing'][1] * 1000)
            if info.get('cached'):
                flags[index] |= _CACHED
                starts[index] = ends[index] = _NO_TIME

    if distbuild_result is not None:
        for uid in distbuild_result.get('failed_results', {}).get('results', []):
            index = indices.get(uid)
            if index is not None:
                flags[index] |= _FAILED

    # Read only UID and the test marker. In particular, never traverse deps,
    # commands or materialize nodes from a compact graph into Python dicts.
    for node in graph['graph']:
        index = indices.get(node['uid'])
        if index is not None:
            flags[index] &= ~_TEST
            if node.get('kv', {}).get('run_test_node', False):
                flags[index] |= _TEST

    counts = dict(
        all_run_tasks_count=len(indices),
        dyn_cached_tasks_count=0,
        failed_task_count=0,
        not_cached_task_count=0,
        tests_task_count=0,
        local_cached_task_count=sum(t == 'get from local cache' for t in prepare_types.values()),
        dist_cached_task_count=sum(t == 'get from dist cache' for t in prepare_types.values()),
    )
    for index, flag in enumerate(flags):
        if flag & _DYNAMIC:
            counts['dyn_cached_tasks_count'] += 1
        if flag & _FAILED:
            counts['failed_task_count'] += 1
        if not flag & (_CACHED | _DYNAMIC) and starts[index] != _NO_TIME and ends[index] != _NO_TIME:
            if ends[index] != starts[index]:
                counts['not_cached_task_count'] += 1
                if flag & _TEST:
                    counts['tests_task_count'] += 1
    return counts
