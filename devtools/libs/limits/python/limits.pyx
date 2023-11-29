cdef extern from "util/system/types.h":
    ctypedef unsigned long ui64

cdef extern from "devtools/libs/limits/limits.h" namespace "NDistBuild":
    ui64 MAX_RESULT_SIZE


def get_max_result_size():
    return MAX_RESULT_SIZE
