from libcpp cimport bool
from cpython.object cimport PyObject
from cpython.unicode cimport PyUnicode_AsUTF8
from util.generic.vector cimport TVector
from util.generic.string cimport TString
from util.datetime.base cimport TDuration
from util.system.types cimport i64, ui64

import logging
import os
from collections.abc import Callable

logger = logging.getLogger(__name__)

ctypedef const char * cstr
ctypedef size_t size_type

cdef extern from 'library/cpp/logger/priority.h':
    ctypedef enum ELogPriority:
        TLOG_EMERG,
        TLOG_ALERT,
        TLOG_CRIT,
        TLOG_ERR,
        TLOG_WARNING,
        TLOG_NOTICE,
        TLOG_INFO,
        TLOG_DEBUG,
        TLOG_RESOURCES


LOG_PRIORITY_TO_LEVEL = {
    TLOG_EMERG: logging.CRITICAL,
    TLOG_ALERT: logging.CRITICAL,
    TLOG_CRIT: logging.CRITICAL,
    TLOG_ERR: logging.ERROR,
    TLOG_WARNING: logging.WARNING,
    TLOG_NOTICE: logging.WARNING,
    TLOG_INFO: logging.INFO,
    TLOG_DEBUG: logging.DEBUG,
    TLOG_RESOURCES: logging.DEBUG,
}


cdef extern void YaYtStoreLoggingHook(ELogPriority priority, const char *msg, size_t size) with gil:
    logger.log(LOG_PRIORITY_TO_LEVEL[priority], msg[:size].decode(errors='replace'))


cdef extern void YaYtStoreDisableHook(void* callback, const TString& errorType, const TString& errorMessage) with gil:
    (<object>callback)(errorType.decode(), errorMessage.decode())


class YtStoreError(Exception):
    pass

cdef public PyObject* yt_store_error = <PyObject*>YtStoreError

cdef extern from *:
    """
        extern PyObject* yt_store_error;
        static void raise_yt_store_error() {
            try {
                throw;
            } catch (const std::exception& e) {
                PyErr_SetString(yt_store_error, e.what());
            }
    }
    """
    cdef void raise_yt_store_error()


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

    cdef cppclass TNameReTtl "NYa::TNameReTtl":
        TNameReTtl(TString nameRe, TDuration ttl)
        TString NameRe
        TDuration Ttl

    cdef cppclass TMaxCacheSize "NYa::TMaxCacheSize":
        TMaxCacheSize(size_t val)
        TMaxCacheSize(double val)

    cdef cppclass TYtStore2Options "NYa::TYtStore2Options":
        TString Token
        bool ReadOnly
        void* OnDisable
        TMaxCacheSize MaxCacheSize
        TDuration Ttl
        TVector[TNameReTtl] NameReTtls
        TString OperationPool
        TDuration RetryTimeLimit
        bool SyncDurability

    cdef cppclass TDataGcOptions "NYa::TDataGcOptions":
        i64 DataSizePerJob
        ui64 DataSizePerKeyRange

    cdef cppclass TYtStore2 "NYa::TYtStore2" nogil:
        TYtStore2(const TString& proxy, const TString& dataDir, const TYtStore2Options& options) except +raise_yt_store_error
        void WaitInitialized() except +raise_yt_store_error
        bool Disabled()
        void Strip() except +raise_yt_store_error
        void DataGc(const TDataGcOptions& options) except +raise_yt_store_error
        @staticmethod
        void ValidateRegexp(const TString& re) except +ValueError

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


cdef class YtStoreWrapper2:
    cdef TYtStore2* store_ptr
    _on_disable: Callable

    def __init__(
        self,
        proxy: str,
        data_dir: str,
        token: str | None,
        readonly: bool = True,
        on_disable: Callable | None = None,
        max_cache_size: int | str | None = None,
        ttl_hours: int | None = None,
        name_re_ttls: dict[str, int] | None = None,
        operation_pool: str | None = None,
        retry_time_limit: float | None = None,
        sync_durability: bool = False,
    ):
        cdef TString c_proxy = proxy.encode()
        cdef TString c_data_dir = data_dir.encode()
        cdef TYtStore2Options options
        if token:
            options.Token = token.encode()
        options.ReadOnly = readonly
        if on_disable:
            self._on_disable = on_disable
            options.OnDisable = <void*> on_disable
        if max_cache_size:
            if isinstance(max_cache_size, int):
                options.MaxCacheSize = TMaxCacheSize(<size_t>max_cache_size)
            else:
                options.MaxCacheSize = TMaxCacheSize(float(max_cache_size))
        if ttl_hours:
            options.Ttl = TDuration.Hours(ttl_hours)
        if name_re_ttls:
            for name_re, ttl in name_re_ttls.items():
                options.NameReTtls.push_back(TNameReTtl(name_re.encode(), TDuration.Hours(ttl)))
        if operation_pool:
            options.OperationPool = operation_pool.encode()
        if retry_time_limit:
            options.RetryTimeLimit = TDuration.MicroSeconds(int((retry_time_limit or 0) * 1_000_000))
        options.SyncDurability = sync_durability
        with nogil:
            self.store_ptr = new TYtStore2(
                c_proxy,
                c_data_dir,
                options
            )

    def wait_initialized(self):
        with nogil:
            self.store_ptr.WaitInitialized()

    def disabled(self) -> bool:
        return self.store_ptr.Disabled()

    def strip(self):
        with nogil:
            self.store_ptr.Strip()

    def data_gc(
        self,
        data_size_per_job: int | None = None,
        data_size_per_key_range: int | None = None,
    ) -> None:
        cdef TDataGcOptions options
        if data_size_per_job:
            options.DataSizePerJob = data_size_per_job
        if data_size_per_key_range:
            options.DataSizePerKeyRange = data_size_per_key_range
        with nogil:
            self.store_ptr.DataGc(options)

    def __dealloc__(self):
        # nogil is required to allow YtStore internal threads write log messages during termination
        with nogil:
            del self.store_ptr

    @staticmethod
    def validate_regexp(re_str: str) -> None:
        TYtStore2.ValidateRegexp(re_str.encode())
