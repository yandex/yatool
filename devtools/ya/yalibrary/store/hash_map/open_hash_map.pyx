import cython
import struct as struct_module

import six
from libc.stdlib cimport malloc, free
from util.generic.string cimport TString
from util.system.types cimport i8, i16, i32, i64, ui8, ui16, ui32, ui64


cdef extern from "devtools/ya/yalibrary/store/hash_map/open_hash_map.h":
    cdef cppclass TOpenHashMapIter:
        bint Next()
        char* Get()

    cdef cppclass TOpenHashMapIterPtr:
        TOpenHashMapIter operator*()

    cdef cppclass TOpenHashMap:
        void SetItem(TString, void*)
        bint GetItem(TString, void*)
        void DelItem(TString)
        TOpenHashMapIterPtr Iter()
        void Flush()

    cdef cppclass TOpenHashMapPtr:
        TOpenHashMap operator*()

    cdef TOpenHashMapPtr CreateOpenHashMap(TString, ui64, ui64)

    cdef i64 SumValues[T](TOpenHashMapPtr)


cdef class OpenHashMap:
    cdef TOpenHashMapPtr _map
    cdef bytes _fmt
    cdef ui64 _data_size

    def __cinit__(self, path, size, fmt):
        self._fmt = six.ensure_binary(fmt)
        self._data_size = struct_module.calcsize(fmt)
        self._map = CreateOpenHashMap(six.ensure_binary(path), size, self._data_size)

    def __setitem__(self, key, values):
        cdef bytes py_data = struct_module.pack(self._fmt, *values)
        cdef char* c_data = py_data
        cython.operator.dereference(self._map).SetItem(six.ensure_binary(key), c_data)

    def __getitem__(self, key):
        cdef bytes py_data
        cdef bint has
        cdef char* data = <char*>malloc(self._data_size)

        try:
            has = cython.operator.dereference(self._map).GetItem(six.ensure_binary(key), data)

            if not has:
                raise KeyError

            py_data = data[:self._data_size]

        finally:
            free(data)

        return struct_module.unpack(self._fmt, py_data)

    def __delitem__(self, key):
        cython.operator.dereference(self._map).DelItem(six.ensure_binary(key))

    def __iter__(self):
        cdef TOpenHashMapIterPtr iterator = cython.operator.dereference(self._map).Iter()
        cdef const char* c_data_ptr
        cdef bytes py_data

        while cython.operator.dereference(iterator).Next():
            c_data_ptr = cython.operator.dereference(iterator).Get()
            py_data = c_data_ptr[:self._data_size]
            yield struct_module.unpack(self._fmt, py_data)

    def sum_values(self):
        if self._fmt.startswith(b'b'):
            return SumValues[i8](self._map)

        elif self._fmt.startswith(b'B'):
            return SumValues[ui8](self._map)

        elif self._fmt.startswith(b'h'):
            return SumValues[i16](self._map)

        elif self._fmt.startswith(b'H'):
            return SumValues[ui16](self._map)

        elif self._fmt.startswith(b'i') or self._fmt.startswith(b'l'):
            return SumValues[i32](self._map)

        elif self._fmt.startswith(b'I') or self._fmt.startswith(b'L'):
            return SumValues[ui32](self._map)

        elif self._fmt.startswith(b'q'):
            return SumValues[i64](self._map)

        elif self._fmt.startswith(b'Q'):
            return SumValues[ui64](self._map)

        else:
            raise NotImplemented

    def flush(self):
        cython.operator.dereference(self._map).Flush()
