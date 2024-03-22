cdef extern from "devtools/executor/proc_util/proc_util.h" namespace "NProcUtil" nogil:
    cdef cppclass TSubreaperApplicant:
        TSubreaperApplicant() except +
        void Close() except +

cdef class SubreaperApplicant:
    cdef TSubreaperApplicant obj

    def __init___(self):
        self.obj = TSubreaperApplicant()

    def close(self):
        self.obj.Close()
