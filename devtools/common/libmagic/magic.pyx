cdef extern from "devtools/common/libmagic/lib/libmagic.h" namespace "NMagic":
    int IsDynLib(const char* filename) nogil
    int IsElfExecutable(const char* filename) nogil
    int IsElf(const char* filename, const char* substr) nogil


def is_dynlib(path):
    return IsDynLib(path)


def is_elf_executable(path):
    return IsElfExecutable(path)


def is_elf(path):
    return IsElf(path, NULL)
