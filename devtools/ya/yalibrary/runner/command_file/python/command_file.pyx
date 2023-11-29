from util.generic.string cimport TString
from util.generic.vector cimport TVector
import six


cdef extern from "devtools/ya/yalibrary/runner/command_file/command_file.h" namespace "NCommandFile":
    cdef cppclass TCommandArgsPacker:
        TCommandArgsPacker() except +
        TCommandArgsPacker(const TString& buildRoot) except +
        TVector[TString] Pack(const TVector[TString]& commandArgs)


cdef class CommandArgsPacker(object):
    cdef TCommandArgsPacker pack_args

    def __cinit__(self, build_root):
        self.pack_args = TCommandArgsPacker(six.ensure_binary(build_root))

    def pack(self, args):
        return list(map(six.ensure_str, self.pack_args.Pack(tuple(map(six.ensure_binary, args)))))

    def apply(self, obj, key):
        if isinstance(obj, dict):
            return {k : self.pack(v) if k == key else self.apply(v, key) for k, v in six.iteritems(obj)}
        if isinstance(obj, list):
            return [self.apply(x, key) for x in obj]
        return obj
