cdef extern from "Python.h":
    char* PyUnicode_AsUTF8(object unicode)

from libcpp cimport bool
from cpython.version cimport PY_VERSION_HEX

import logging
import os

logger = logging.getLogger(__name__)

cdef extern void YaYtStoreLoggingHook(const char *msg) with gil:
    logger.debug(str(msg))

cdef extern from 'devtools/ya/yalibrary/store/yt_store/xx_client.hpp':
    cdef cppclass YtStoreClientResponse nogil:
        bool Success;
        size_t DecodedSize;
        char ErrorMsg[4096];
    cdef cppclass YtStoreClientRequest nogil:
        const char *Hash;
        const char *IntoDir;
        const char *Codec;
        size_t DataSize;
        int Chunks;
    cdef cppclass YtStore:
        YtStore(const char *yt_proxy, const char *yt_dir, const char *yt_token) except +
        void DoTryRestore(const YtStoreClientRequest &req, YtStoreClientResponse &rsp) nogil

cdef class YtStoreWrapper:
    cdef YtStore *c_ytstore

    def __init__(self, yt_proxy, yt_dir, yt_token):
        self.c_ytstore = new YtStore(
            PyUnicode_AsUTF8(yt_proxy),
            PyUnicode_AsUTF8(yt_dir),
            PyUnicode_AsUTF8(yt_token or ""),
        )

    def do_try_restore(self, shash, into_dir, codec, chunks_count, data_size):
        cdef YtStoreClientRequest req
        req.Hash = PyUnicode_AsUTF8(shash)
        req.IntoDir = PyUnicode_AsUTF8(into_dir)
        req.Codec = PyUnicode_AsUTF8(codec or '')
        req.Chunks = chunks_count
        req.DataSize = data_size
        cdef YtStoreClientResponse resp
        with nogil:
            self.c_ytstore.DoTryRestore(
                req,
                resp
            )
        if resp.ErrorMsg[0] and not resp.Success:
            errmsg = str(resp.ErrorMsg)
            raise Exception(errmsg)
        result = resp.DecodedSize
        return result

    def __dealloc__(self):
        del self.c_ytstore

