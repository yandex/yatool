"""Read-only CSR reverse topology with borrowed Python UID strings.

The temporary UID->integer dict is discarded after construction. Lookups use
binary search in a sorted tuple of existing strings; edges are uint32 IDs.
Each adjacency view is lazy and already sorted in Python UID order.
"""
from collections.abc import Sequence
import operator
import sys
from time import perf_counter

from devtools.ya.yalibrary.sjson.compact cimport (
    CompactKindBits, CompactKindMask, GetImmutableList, List, Map, String, TCompactStorage, TIndexType,
)


cdef TIndexType _field(TCompactStorage* storage, TIndexType node, object key) except *:
    cdef TIndexType result
    if not storage.MapGet(node, key, &result):
        raise KeyError(key)
    return result


cdef double _checkpoint(dict timings, str stage, double previous) except -1:
    cdef double now
    if timings is None:
        return 0
    now = perf_counter()
    timings[stage] = now - previous
    return now


cdef uint32_t _MISSING_ID = <uint32_t>-1


cdef tuple _remap_uids(TReverseIndex* native, dict uid_to_id, vector[uint32_t]* string_to_id):
    """Assign IDs in UID sort order and update the lookup used by the fill pass."""
    cdef tuple uids = tuple(sorted(uid_to_id))
    cdef vector[uint32_t] old_to_new
    cdef Py_ssize_t position
    cdef size_t i
    cdef uint32_t old_id
    old_to_new.resize(len(uids))
    for position, uid in enumerate(uids):
        old_to_new[uid_to_id[uid]] = position
        if string_to_id == NULL:
            uid_to_id[uid] = position
    native.RemapCounts(old_to_new)
    if string_to_id != NULL:
        for i in range(string_to_id[0].size()):
            old_id = string_to_id[0][i]
            if old_id != _MISSING_ID:
                string_to_id[0][i] = old_to_new[old_id]
    return uids


cdef tuple _build_from_sequence(TReverseIndex* native, object graph_nodes, dict timings, double started):
    """Read Python mappings, including compact nodes with overlays."""
    cdef dict uid_to_id = {}
    cdef str uid, dependency
    cdef uint32_t node_id, dependency_id
    cdef object existing

    for node in graph_nodes:
        uid = node['uid']
        if uid not in uid_to_id:
            native.AddNode()
            uid_to_id[uid] = len(uid_to_id)
    cdef double checkpoint = _checkpoint(timings, 'uids', started)

    for node in graph_nodes:
        for dependency in node['deps']:
            existing = uid_to_id.get(dependency)
            if existing is None:
                native.AddNode()
                existing = len(uid_to_id)
                uid_to_id[dependency] = existing
            native.CountEdge(existing)
    checkpoint = _checkpoint(timings, 'count_python', checkpoint)

    cdef tuple uids = _remap_uids(native, uid_to_id, NULL)
    checkpoint = _checkpoint(timings, 'sort_uids_and_remap', checkpoint)
    native.BeginFill()
    checkpoint = _checkpoint(timings, 'allocate_cpp', checkpoint)

    for node in graph_nodes:
        node_id = uid_to_id[node['uid']]
        for dependency in node['deps']:
            dependency_id = uid_to_id[dependency]
            native.AddEdge(dependency_id, node_id)
    _checkpoint(timings, 'fill_python', checkpoint)
    return uids


cdef tuple _build_from_compact(
    TReverseIndex* native, TCompactStorage* storage, TIndexType root, dict timings, double started,
):
    """Use native string IDs without boxing nodes or dependency lists."""
    cdef dict uid_to_id = {}
    cdef vector[uint32_t] string_to_id
    cdef size_t i, j
    cdef TIndexType node_value, uid_value, deps_value, dependency_value
    cdef uint32_t node_id, dependency_id
    cdef str uid
    cdef object existing
    cdef object uid_key = sys.intern('uid')
    cdef object deps_key = sys.intern('deps')

    string_to_id.assign(storage.StringCount(), _MISSING_ID)
    for i in range(storage.Size(root)):
        node_value = storage.Item(root, i)
        if (node_value & CompactKindMask) != Map:
            raise TypeError('Graph nodes must be mappings')
        uid_value = _field(storage, node_value, uid_key)
        if (uid_value & CompactKindMask) != String:
            raise TypeError('Graph UIDs must be strings')
        uid = <object>storage.StringObject(uid_value)
        existing = uid_to_id.get(uid)
        if existing is None:
            native.AddNode()
            existing = len(uid_to_id)
            uid_to_id[uid] = existing
        string_to_id[uid_value >> CompactKindBits] = existing
    cdef double checkpoint = _checkpoint(timings, 'uids', started)

    for i in range(storage.Size(root)):
        node_value = storage.Item(root, i)
        deps_value = _field(storage, node_value, deps_key)
        if (deps_value & CompactKindMask) != List:
            raise TypeError('Graph dependencies must be lists')
        for j in range(storage.Size(deps_value)):
            dependency_value = storage.Item(deps_value, j)
            if (dependency_value & CompactKindMask) != String:
                raise TypeError('Graph dependency UIDs must be strings')
            dependency_id = string_to_id[dependency_value >> CompactKindBits]
            if dependency_id == _MISSING_ID:
                native.AddNode()
                dependency_id = len(uid_to_id)
                uid = <object>storage.StringObject(dependency_value)
                uid_to_id[uid] = dependency_id
                string_to_id[dependency_value >> CompactKindBits] = dependency_id
            native.CountEdge(dependency_id)
    checkpoint = _checkpoint(timings, 'count_native', checkpoint)

    cdef tuple uids = _remap_uids(native, uid_to_id, &string_to_id)
    checkpoint = _checkpoint(timings, 'sort_uids_and_remap', checkpoint)
    native.BeginFill()
    checkpoint = _checkpoint(timings, 'allocate_cpp', checkpoint)

    for i in range(storage.Size(root)):
        node_value = storage.Item(root, i)
        uid_value = _field(storage, node_value, uid_key)
        node_id = string_to_id[uid_value >> CompactKindBits]
        deps_value = _field(storage, node_value, deps_key)
        for j in range(storage.Size(deps_value)):
            dependency_value = storage.Item(deps_value, j)
            native.AddEdge(string_to_id[dependency_value >> CompactKindBits], node_id)
    _checkpoint(timings, 'fill_native', checkpoint)
    return uids


cdef class ReverseIndex:
    def __cinit__(self):
        self._native = new TReverseIndex()
        self._uids = ()

    def __dealloc__(self):
        del self._native

    def __init__(self, graph_nodes, bint profile=False):
        cdef TCompactStorage* storage = NULL
        cdef TIndexType root
        cdef double started = perf_counter() if profile else 0
        cdef double checkpoint
        cdef dict timings = {} if profile else None
        if self._started:
            raise TypeError('ReverseIndex is immutable and cannot be reinitialized')
        self._started = True
        if not isinstance(graph_nodes, Sequence):
            raise TypeError('ReverseIndex requires a repeatable sequence of graph nodes')

        self._compact_input = GetImmutableList(graph_nodes, &storage, &root)
        if self._compact_input:
            self._uids = _build_from_compact(self._native, storage, root, timings, started)
        else:
            self._uids = _build_from_sequence(self._native, graph_nodes, timings, started)

        checkpoint = perf_counter() if profile else 0
        with nogil:
            self._native.Finish()
        if profile:
            timings['finish_cpp'] = perf_counter() - checkpoint
            timings['total_before_cleanup'] = perf_counter() - started
            self._build_timings = timings
        self._ready = True

    @property
    def build_timings(self):
        self._check_ready()
        return dict(self._build_timings or {})

    cdef void _check_ready(self) except *:
        if not self._ready:
            raise RuntimeError('ReverseIndex construction did not finish')

    cdef Py_ssize_t _lookup(self, object uid) except -2:
        cdef Py_ssize_t left = 0
        cdef Py_ssize_t right = len(self._uids)
        cdef Py_ssize_t middle
        self._check_ready()
        if not isinstance(uid, str):
            return -1
        while left < right:
            middle = left + (right - left) // 2
            if self._uids[middle] < uid:
                left = middle + 1
            else:
                right = middle
        if left == len(self._uids) or self._uids[left] != uid:
            return -1
        if self._native.Begin(left) == self._native.End(left):
            return -1
        return left

    def __len__(self):
        self._check_ready()
        return self._native.KeyCount()

    def __iter__(self):
        cdef Py_ssize_t i
        self._check_ready()
        for i in range(len(self._uids)):
            if self._native.Begin(i) != self._native.End(i):
                yield self._uids[i]

    def __contains__(self, uid):
        return self._lookup(uid) >= 0

    def __getitem__(self, uid):
        cdef Py_ssize_t position = self._lookup(uid)
        cdef _Dependents result
        if position < 0:
            raise KeyError(uid)
        result = _Dependents.__new__(_Dependents)
        result._owner = self
        result._begin = self._native.Begin(position)
        result._end = self._native.End(position)
        return result

    def get(self, uid, default=None):
        cdef Py_ssize_t position = self._lookup(uid)
        cdef _Dependents result
        if position < 0:
            return default
        result = _Dependents.__new__(_Dependents)
        result._owner = self
        result._begin = self._native.Begin(position)
        result._end = self._native.End(position)
        return result

    @property
    def storage_stats(self):
        self._check_ready()
        return {'node_ids': len(self._uids), 'reverse_keys': self._native.KeyCount(),
                'edges': self._native.EdgeCount(), 'native_bytes': self._native.NativeBytes(),
                'uid_tuple_bytes': sys.getsizeof(self._uids), 'compact_input': bool(self._compact_input)}


cdef class _Dependents:
    def __len__(self):
        return self._end - self._begin

    def __iter__(self):
        cdef uint32_t i
        for i in range(self._begin, self._end):
            yield self._owner._uids[self._owner._native.Edge(i)]

    def __getitem__(self, key):
        cdef Py_ssize_t position = operator.index(key)
        if position < 0:
            position += len(self)
        if position < 0 or position >= len(self):
            raise IndexError(key)
        return self._owner._uids[self._owner._native.Edge(self._begin + position)]
