cdef extern from "Python.h":
    char* PyUnicode_AsUTF8(object unicode)

from libcpp cimport bool
from cpython.version cimport PY_VERSION_HEX
from util.generic.vector cimport TVector
from util.datetime.base cimport TDuration

import logging
import os

logger = logging.getLogger(__name__)

ctypedef const char * cstr

cdef extern void YaYtStoreLoggingHook(const char *msg) with gil:
    logger.debug(str(msg))

cdef extern from 'devtools/ya/yalibrary/store/yt_store/xx_client.hpp':
    cdef cppclass YtStoreClientResponse nogil:
        bool Success;
        bool NetworkErrors;
        size_t DecodedSize;
        char ErrorMsg[4096];
    cdef cppclass YtStoreClientRequest nogil:
        const char *Hash;
        const char *IntoDir;
        const char *Codec;
        size_t DataSize;
        int Chunks;
    cdef cppclass YtStorePrepareDataRequest nogil:
        const char* OutPath;
        const char* Codec;
        const char* RootDir;
        TVector[cstr] Files;
    cdef cppclass YtStorePrepareDataResponse nogil:
        bool Success;
        size_t RawSize;
        char ErrorMsg[4096];
    cdef cppclass YtStore:
        YtStore(const char *yt_proxy, const char *yt_dir, const char *yt_token, TDuration retry_time_limit) except +
        void DoTryRestore(const YtStoreClientRequest &req, YtStoreClientResponse &rsp) nogil
        void PrepareData(const YtStorePrepareDataRequest& req, YtStorePrepareDataResponse& rsp) nogil

class NetworkException(Exception):
    pass

cdef class YtStoreWrapper:
    cdef YtStore *c_ytstore

    def __init__(self, yt_proxy, yt_dir, yt_token, retry_time_limit):
        self.c_ytstore = new YtStore(
            PyUnicode_AsUTF8(yt_proxy),
            PyUnicode_AsUTF8(yt_dir),
            PyUnicode_AsUTF8(yt_token or ""),
            TDuration.MicroSeconds(int((retry_time_limit or 0) * 1_000_000))
        )

    def do_try_restore(self, shash, into_dir, codec, chunks_count, data_size):
        cdef YtStoreClientResponse resp
        cdef YtStoreClientRequest req
        req.Hash = PyUnicode_AsUTF8(shash)
        req.IntoDir = PyUnicode_AsUTF8(into_dir)
        req.Codec = PyUnicode_AsUTF8(codec or '')
        req.Chunks = chunks_count
        req.DataSize = data_size
        with nogil:
            self.c_ytstore.DoTryRestore(req, resp)
        if resp.ErrorMsg[0] and not resp.Success:
            errmsg = str(resp.ErrorMsg)
            raise (NetworkException if resp.NetworkErrors else Exception)(errmsg)
        result = resp.DecodedSize
        return result

    def prepare_data(self, out_path, files, codec, root_dir):
        cdef YtStorePrepareDataResponse resp
        cdef YtStorePrepareDataRequest req
        req.OutPath = PyUnicode_AsUTF8(out_path)
        req.Codec = PyUnicode_AsUTF8(codec or '')
        req.RootDir = PyUnicode_AsUTF8(root_dir)
        req.Files = TVector[cstr]()
        for file in sorted(files):
            req.Files.push_back(PyUnicode_AsUTF8(file))

        with nogil:
            self.c_ytstore.PrepareData(req, resp)

        if resp.ErrorMsg[0] and not resp.Success:
            errmsg = str(resp.ErrorMsg)
            raise Exception(errmsg)
        return resp.RawSize

    def __dealloc__(self):
        del self.c_ytstore

