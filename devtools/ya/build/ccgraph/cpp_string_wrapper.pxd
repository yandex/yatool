from libcpp cimport bool
from util.generic.string cimport TString

cdef class CppStringWrapper:
    cdef TString output
    cdef bool compressed
