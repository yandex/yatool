from util.generic.string cimport TString
from util.folder.path cimport TFsPath


cdef extern from "<devtools/libs/acdigest/acdigest.h>" namespace "NACDigest" nogil:
    cdef cppclass TFileDigest:
        TString ContentDigest
        TString Uid
        size_t Size

    cdef TFileDigest GetFileDigest(TFsPath&, TString contentDigest) except +
    cdef TFileDigest GetFileDigest(TFsPath&) except +
    cdef TString GetBufferDigest(const char* ptr, size_t size) except +

    cdef int DIGEST_GIT_LIKE_VERSION
    cdef int DIGEST_XXHASH_VERSION
    cdef int DIGEST_CURRENT_VERSION
