from cpython.ref cimport PyObject
from libc.stddef cimport size_t
from libcpp cimport bool

cdef extern from "devtools/ya/yalibrary/sjson/lib/compact.h" namespace "NSJson":
    # Extern typedef declarations and pointer arguments use NSJson::TIndexType
    # in generated C++, so locals have the header's actual ui32/ui64 width.
    # The widest integer here also keeps Python conversions valid for ui64.
    ctypedef unsigned long long TIndexType
    const TIndexType CompactKindMask
    cdef enum ECompactKind:
        List
        Map

    cppclass TCompactStorage:
        TCompactStorage(bool keys, bool values) except +
        size_t Size(TIndexType value)
        TIndexType Item(TIndexType value, size_t index)
        TIndexType MapValue(TIndexType value, size_t index)
        object MapKey(TIndexType value, size_t index)
        bool MapGet(TIndexType value, object key, TIndexType* result) except +
        object Scalar(TIndexType value)
        size_t NativeBytes()
        size_t StringCount()
        size_t ListCount()
        size_t MapCount()
        size_t ShapeCount()
        size_t ValueCount()
        size_t SharedBlocks()

    void EncodeCompact(object stream, const TCompactStorage& storage, TIndexType value,
                        PyObject* owner, PyObject* (*overrideValue)(PyObject*, TIndexType) except NULL) except +

cdef class _GraphStorage:
    cdef TCompactStorage* native
    cdef dict overrides

cdef PyObject* _compact_factory(PyObject* storage, TIndexType value) except NULL
