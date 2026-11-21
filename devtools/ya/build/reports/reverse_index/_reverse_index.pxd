from libc.stddef cimport size_t
from libc.stdint cimport uint32_t
from libcpp.vector cimport vector

cdef extern from "devtools/ya/build/reports/reverse_index/lib/reverse_index.h" namespace "NYa::NReverseIndex":
    cdef cppclass TReverseIndex:
        TReverseIndex() except +
        void AddNode() except +
        void CountEdge(uint32_t) except +
        void RemapCounts(const vector[uint32_t]&) except +
        void BeginFill() except +
        void AddEdge(uint32_t, uint32_t) except +
        void Finish() except + nogil
        uint32_t Begin(uint32_t) const
        uint32_t End(uint32_t) const
        uint32_t Edge(uint32_t) const
        size_t KeyCount() const
        size_t EdgeCount() const
        size_t NativeBytes() const

cdef class ReverseIndex:
    cdef TReverseIndex* _native
    cdef tuple _uids
    cdef bint _started
    cdef bint _ready
    cdef dict _build_timings
    cdef bint _compact_input
    cdef void _check_ready(self) except *
    cdef Py_ssize_t _lookup(self, object uid) except -2

cdef class _Dependents:
    cdef ReverseIndex _owner
    cdef uint32_t _begin
    cdef uint32_t _end
