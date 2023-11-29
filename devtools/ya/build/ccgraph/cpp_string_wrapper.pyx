from util.generic.string cimport TString, TStringBuf

import six


cdef class CppStringWrapper:
    def __cinit__(self):
        self.output = ""
        self.compressed = False

    def set_compressed(self, compressed):
        self.compressed = compressed

    def append(self, s):
        # for debug purpose
        cdef TStringBuf buf = TStringBuf(s, len(s))
        self.output += buf

    def is_compressed(self):
        # for test purpose
        return self.compressed

    def raw_output(self):
        # for test purpose
        return six.ensure_binary(self.output)

    def __len__(self):
        # for debug purpose
        return self.output.size()
