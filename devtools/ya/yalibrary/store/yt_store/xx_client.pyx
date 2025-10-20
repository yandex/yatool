from cpython.object cimport PyObject
from cpython.unicode cimport PyUnicode_AsUTF8
from cython.operator cimport dereference as deref
from cython.operator cimport preincrement as preinc
from libcpp cimport bool
from libcpp.atomic cimport atomic, memory_order
from libcpp.optional cimport optional
from libcpp.pair cimport pair
from util.datetime.base cimport TDuration, TInstant
from util.folder.path cimport TFsPath
from util.generic.hash cimport THashMap
from util.generic.ptr cimport TIntrusivePtr
from util.generic.string cimport TString
from util.generic.vector cimport TVector
from util.system.types cimport i64, ui64

import atexit
import logging
import os
import time
import typing as tp
from collections.abc import Callable

from devtools.ya.core import config as core_config
from devtools.ya.core import monitoring as core_monitoring
from devtools.ya.core import report
from devtools.ya.core import stage_tracer
from yalibrary.store.yt_store import consts


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


cdef extern void YaYtStoreDisableHook(void* owner, const TString& errorType, const TString& errorMessage) with gil:
    (<YtStore2Impl>owner)._on_disable_callback(errorType.decode(), errorMessage.decode())


cdef extern void YaYtStoreStartStage(void* owner, const TString& name) with gil:
    if (<YtStore2Impl>owner)._stager:
        (<YtStore2Impl>owner)._stager.start(name.decode())


cdef extern void YaYtStoreFinishStage(void* owner, const TString& name) with gil:
    if (<YtStore2Impl>owner)._stager:
        (<YtStore2Impl>owner)._stager.finish(name.decode())


class YtStoreError(Exception):
    def __init__(self, message, mute=False):
        super().__init__(message)
        self.mute = mute

cdef public PyObject* yt_store_error = <PyObject*>YtStoreError

# include NYa::TYtStoreError definition
cdef extern from 'devtools/ya/yalibrary/store/yt_store/xx_client.hpp':
    pass

cdef extern from *:
    r"""
        extern PyObject* yt_store_error;
        static void raise_yt_store_error() {
            try {
                throw;
            } catch (const NYa::TYtStoreError& e) {
                PyObject* err = PyObject_CallFunction(yt_store_error, "sO", e.what(), e.Mute ? Py_True : Py_False);
                if (!err) {
                    return;
                }
                PyErr_SetObject(yt_store_error, err);
            } catch (const std::exception& e) {
                TString error = TypeName(e) + ": " + e.what();
                PyErr_SetString(yt_store_error, error.c_str());
            } catch (...) {
                TString error = CurrentExceptionMessage();
                PyErr_SetString(yt_store_error, error.c_str());
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

cdef extern from "devtools/ya/yalibrary/store/yt_store/xx_client.hpp" namespace "NYa":
    cdef void AtExit()

    cdef cppclass TNameReTtl:
        TNameReTtl(TString nameRe, TDuration ttl)
        TString NameRe
        TDuration Ttl

    cdef cppclass TMaxCacheSize:
        TMaxCacheSize(size_t val)
        TMaxCacheSize(double val)

    cdef cppclass TYtTokenOption:
        TString Token

    enum ECritLevel:
        NONE "NYa::ECritLevel::NONE"
        GET "NYa::ECritLevel::GET"
        PUT "NYa::ECritLevel::PUT"

    cdef cppclass TYtStore2Options(TYtTokenOption):
        void* Owner
        bool ReadOnly
        bool CheckSize
        TMaxCacheSize MaxCacheSize
        TDuration Ttl
        TVector[TNameReTtl] NameReTtls
        TString OperationPool
        TDuration RetryTimeLimit
        TDuration InitTimeout
        TDuration PrepareTimeout
        bool ProbeBeforePut
        size_t ProbeBeforePutMinSize
        ECritLevel CritLevel

    cdef cppclass TYtStore2 nogil:
        ctypedef TVector[TString] TUidList

        cppclass TPrepareOptions:
            TPrepareOptions()
            TUidList SelfUids
            TUidList Uids
            bool RefreshOnRead
            bool ContentUidsEnabled

        ctypedef TIntrusivePtr[TPrepareOptions] TPrepareOptionsPtr

        cppclass TPutOptions:
            TPutOptions()
            TString SelfUid
            TString Uid
            TFsPath RootDir
            TVector[TFsPath] Files
            TString Codec
            TString Cuid
            size_t ForcedSize

        cppclass TMetrics:
            TMetrics()
            THashMap[TString, TDuration] Timers
            THashMap[TString, TVector[pair[TInstant, TInstant]]] TimerIntervals
            THashMap[TString, int] Counters
            THashMap[TString, int] Failures
            THashMap[TString, size_t] DataSize
            # Cache hit
            int Requested
            int Found
            # CompressionRatio
            size_t TotalCompressedSize
            size_t TotalRawSize
            # Additional metrics
            TInstant TimeToFirstCallHas
            TInstant TimeToFirstRecvMeta

        cppclass TDataGcOptions:
            TDataGcOptions()
            i64 DataSizePerJob
            ui64 DataSizePerKeyRange

        cppclass TCreateTablesOptions(TYtTokenOption):
            TCreateTablesOptions()
            int Version
            bool Replicated
            bool Tracked
            bool InMemory
            bool Mount
            bool IgnoreExisting
            optional[ui64] MetadataTabletCount
            optional[ui64] DataTabletCount

        cppclass TModifyTablesStateOptions(TYtTokenOption):
            enum EAction:
                MOUNT "NYa::TYtStore2::TModifyTablesStateOptions::EAction::MOUNT"
                UNMOUNT "NYa::TYtStore2::TModifyTablesStateOptions::EAction::UNMOUNT"

            TModifyTablesStateOptions()
            EAction Action

        cppclass TModifyReplicaOptions(TYtTokenOption):
            enum EAction:
                CREATE "NYa::TYtStore2::TModifyReplicaOptions::EAction::CREATE"
                REMOVE "NYa::TYtStore2::TModifyReplicaOptions::EAction::REMOVE"

            TModifyReplicaOptions()
            EAction Action
            optional[bool] SyncMode
            optional[bool] Enable


        TYtStore2(const TString& proxy, const TString& dataDir, const TYtStore2Options& options) except +raise_yt_store_error
        bool Disabled()
        bool ReadOnly()
        void Prepare(TPrepareOptionsPtr options) except +raise_yt_store_error
        bool Has(const TString& uid) except +raise_yt_store_error
        bool TryRestore(const TString& uid, const TString& intoDir) except +raise_yt_store_error
        bool Put(const TPutOptions& options) except +raise_yt_store_error
        void Strip() except +raise_yt_store_error
        void DataGc(const TDataGcOptions& options) except +raise_yt_store_error
        TMetrics GetMetrics() except +raise_yt_store_error
        void Shutdown()
        @staticmethod
        void ValidateRegexp(const TString& re) except +ValueError
        @staticmethod
        void CreateTables(const TString& proxy, const TString& dataDir, const TCreateTablesOptions& options) except +raise_yt_store_error
        @staticmethod
        void ModifyTablesState(const TString& proxy, const TString& dataDir, const TModifyTablesStateOptions& options) except +raise_yt_store_error
        @staticmethod
        void ModifyReplica(
            const TString& proxy,
            const TString& dataDir,
            const TString& replicaProxy,
            const TString& replicaDataDir,
            const TModifyReplicaOptions& options,
        ) except +raise_yt_store_error


@atexit.register
def on_exit():
    AtExit()


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


class YtStoreMetrics:
    def __init__(self):
        self.timers: dict[str, float] = {}
        self.time_intervals: dict[str, list[tuple[float, float]]] = {}
        self.counters: dict[str, int] = {}
        self.failures: dict[str, int] = {}
        self.data_size: dict[str, int] = {}
        # Cache hit
        self.requested = 0
        self.found= 0
        # CompressionRatio
        self.total_compressed_size = 0
        self.total_raw_size = 0
        # additional metrics
        self.time_to_first_call_has = 0.0
        self.time_to_first_recv_meta = 0.0


cdef class YtStore2Impl:
    cdef TYtStore2* _store_ptr
    _proxy: str
    _data_dir: str
    _is_heater: bool
    _stager: stage_tracer.StageTracer.GroupStageTracer | None
    _exiting: bool

    def __init__(
        self,
        proxy: str,
        data_dir: str,
        token: str | None,
        readonly: bool,
        check_size: bool,
        max_cache_size: int | str | None,
        ttl_hours: int | None,
        name_re_ttls: dict[str, int] | None,
        operation_pool: str | None,
        retry_time_limit: float | None,
        init_timeout: float | None,
        prepare_timeout: float | None,
        probe_before_put: bool,
        probe_before_put_min_size: int | None,
        crit_level: str | None,
        stager: stage_tracer.StageTracer.GroupStageTracer | None,
    ):
        self._proxy = proxy
        self._data_dir = data_dir
        self._is_heater = crit_level == "put"
        self._stager = stager
        self._exiting = False
        atexit.register(self._at_exit)

        cdef TString c_proxy = proxy.encode()
        cdef TString c_data_dir = data_dir.encode()
        cdef TYtStore2Options options
        if token:
            options.Token = token.encode()
        options.Owner = <void*> self
        options.ReadOnly = readonly
        options.CheckSize = check_size
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
            options.RetryTimeLimit = YtStore2Impl._as_duration(retry_time_limit)
        if init_timeout:
            options.InitTimeout = YtStore2Impl._as_duration(init_timeout)
        if prepare_timeout:
            options.PrepareTimeout = YtStore2Impl._as_duration(prepare_timeout)
        options.ProbeBeforePut = probe_before_put
        if probe_before_put_min_size:
            options.ProbeBeforePutMinSize = probe_before_put_min_size
        if crit_level:
            if crit_level == "get":
                options.CritLevel = ECritLevel.GET
            elif crit_level == "put":
                options.CritLevel = ECritLevel.PUT
            else:
                raise ValueError(f"Unknown crit_level value: {crit_level}")
        with nogil:
            self._store_ptr = new TYtStore2(
                c_proxy,
                c_data_dir,
                options
            )

    def __dealloc__(self):
        if self._exiting:
            # Let OS destroy everything
            self._store_ptr = NULL
        else:
            # nogil is required to allow YtStore internal threads write log messages during termination
            with nogil:
                del self._store_ptr

    def disabled(self) -> bool:
        return self._store_ptr.Disabled()

    def readonly(self) -> bool:
        return self._store_ptr.ReadOnly()

    def fits(self, node):
        if isinstance(node, dict):
            outputs = node["outputs"]
            kv = node.get("kv") or {}
        else:
            outputs = node.outputs
            kv = node.kv or {}

        if not len(outputs):
            return False

        p = kv.get('p')
        if p in consts.YT_CACHE_EXCLUDED_P:
            return False
        for o in outputs:
            for p in 'library/cpp/svnversion', 'library/cpp/build_info':
                if o.startswith('$(BUILD_ROOT)/' + p):
                    return False
        if all(o.endswith('.tar') for o in outputs):
            return False
        return True

    def prepare(self,
        self_uids: list[str],
        uids: list[str],
        refresh_on_read=False,
        content_uids=False
    ) -> None:
        cdef TYtStore2.TPrepareOptionsPtr options = new TYtStore2.TPrepareOptions()
        for u in self_uids:
            options.Get().SelfUids.push_back(u.encode())
        for u in uids:
            options.Get().Uids.push_back(u.encode())
        options.Get().RefreshOnRead = refresh_on_read
        options.Get().ContentUidsEnabled = content_uids
        with nogil:
            self._store_ptr.Prepare(options)

    def _do_has(self, uid: str) -> bool:
        cdef TString c_uid = uid.encode()
        cdef bool c_result
        with nogil:
            c_result = self._store_ptr.Has(c_uid)
        return c_result

    def _do_try_restore(self, uid: str, into_dir: str, *args, **kwargs) -> bool:
        cdef TString c_uid = uid.encode()
        cdef TString c_into_dir = into_dir.encode()
        cdef bool c_result
        with nogil:
            c_result = self._store_ptr.TryRestore(c_uid, c_into_dir)
        return c_result

    def _do_put(
        self,
        self_uid: str,
        uid: str,
        root_dir: str,
        files: list[str],
        codec: str | None = None,
        cuid: str | None = None,
        forced_size: int | None = None
    ) -> bool:
        cdef TYtStore2.TPutOptions options
        options.SelfUid = self_uid.encode()
        options.Uid = uid.encode()
        options.RootDir = TFsPath(<TString>(root_dir.encode()))
        for f in files:
            options.Files.push_back(TFsPath(<TString>(f.encode())))
        if codec:
            options.Codec = codec.encode()
        if cuid:
            options.Cuid = cuid.encode()
        if forced_size:
            options.ForcedSize = forced_size
        cdef bool c_result
        with nogil:
            c_result = self._store_ptr.Put(options)
        return c_result

    def get_metrics(self) -> YtStoreMetrics:
        cdef TYtStore2.TMetrics c_metrics = self._store_ptr.GetMetrics()

        metrics = YtStoreMetrics()

        timers_it = c_metrics.Timers.begin()
        while timers_it != c_metrics.Timers.end():
            metrics.timers[deref(timers_it).first.decode()] = deref(timers_it).second.SecondsFloat()
            preinc(timers_it)

        cdef TVector[pair[TInstant, TInstant]].iterator int_it
        time_int_it = c_metrics.TimerIntervals.begin()
        while time_int_it != c_metrics.TimerIntervals.end():
            intervals = metrics.time_intervals[deref(time_int_it).first.decode()] = []
            int_it = deref(time_int_it).second.begin()
            while int_it != deref(time_int_it).second.end():
                intervals.append((deref(int_it).first.SecondsFloat(), deref(int_it).second.SecondsFloat()))
                preinc(int_it)
            preinc(time_int_it)

        counters_it = c_metrics.Counters.begin()
        while counters_it != c_metrics.Counters.end():
            metrics.counters[deref(counters_it).first.decode()] = deref(counters_it).second
            preinc(counters_it)

        failures_it = c_metrics.Failures.begin()
        while failures_it != c_metrics.Failures.end():
            metrics.failures[deref(failures_it).first.decode()] = deref(failures_it).second
            preinc(failures_it)

        data_size_it = c_metrics.DataSize.begin()
        while data_size_it != c_metrics.DataSize.end():
            metrics.data_size[deref(data_size_it).first.decode()] = deref(data_size_it).second
            preinc(data_size_it)

        metrics.requested = c_metrics.Requested
        metrics.found = c_metrics.Found

        metrics.total_compressed_size = c_metrics.TotalCompressedSize
        metrics.total_raw_size = c_metrics.TotalRawSize

        metrics.time_to_first_call_has = c_metrics.TimeToFirstCallHas.SecondsFloat()
        metrics.time_to_first_recv_meta = c_metrics.TimeToFirstRecvMeta.SecondsFloat()

        return metrics

    @property
    def avg_compression_ratio(self):
        metrics = self.get_metrics()
        if metrics.total_raw_size:
            return metrics.total_compressed_size / metrics.total_raw_size
        return 1.0

    def strip(self):
        with nogil:
            self._store_ptr.Strip()

    def data_gc(
        self,
        data_size_per_job: int | None = None,
        data_size_per_key_range: int | None = None,
    ) -> None:
        cdef TYtStore2.TDataGcOptions options
        if data_size_per_job:
            options.DataSizePerJob = data_size_per_job
        if data_size_per_key_range:
            options.DataSizePerKeyRange = data_size_per_key_range
        with nogil:
            self._store_ptr.DataGc(options)

    @staticmethod
    def validate_regexp(re_str: str) -> None:
        TYtStore2.ValidateRegexp(re_str.encode())

    @staticmethod
    def create_tables(
        proxy: str,
        data_dir: str,
        version: int,
        token: str | None = None,
        replicated: bool = False,
        tracked: bool = False,
        in_memory: bool = False,
        mount: bool = False,
        ignore_existing: bool = False,
        metadata_tablet_count: int | None = None,
        data_tablet_count: int | None = None,
    ):
        cdef TString c_proxy = proxy.encode()
        cdef TString c_data_dir = data_dir.encode()
        cdef TYtStore2.TCreateTablesOptions options
        cdef ui64 c_metadata_tablet_count
        cdef ui64 c_data_tablet_count
        if token:
            options.Token = token.encode()
        options.Version = version
        options.Replicated = replicated
        options.Tracked = tracked
        options.InMemory = in_memory
        options.Mount = mount
        options.IgnoreExisting = ignore_existing
        if metadata_tablet_count is not None:
            # Cython cannot cast python object to the std::optional
            c_metadata_tablet_count = metadata_tablet_count
            options.MetadataTabletCount = c_metadata_tablet_count
        if data_tablet_count is not None:
            # Cython cannot cast python object to the std::optional
            c_data_tablet_count = data_tablet_count
            options.DataTabletCount = c_data_tablet_count

        TYtStore2.CreateTables(c_proxy, c_data_dir, options)

    @staticmethod
    def _change_state(proxy: str, data_dir: str, action: TYtStore2.TModifyTablesStateOptions.EAction, token: str | None):
        cdef TString c_proxy = proxy.encode()
        cdef TString c_data_dir = data_dir.encode()
        cdef TYtStore2.TModifyTablesStateOptions options
        if token:
            options.Token = token.encode()
        options.Action = action
        TYtStore2.ModifyTablesState(c_proxy, c_data_dir, options)

    @staticmethod
    def mount(proxy: str, data_dir: str, token: str | None = None):
        YtStore2Impl._change_state(proxy, data_dir, TYtStore2.TModifyTablesStateOptions.EAction.MOUNT, token)

    @staticmethod
    def unmount(proxy: str, data_dir: str, token: str | None = None):
        YtStore2Impl._change_state(proxy, data_dir, TYtStore2.TModifyTablesStateOptions.EAction.UNMOUNT, token)

    @staticmethod
    def setup_replica(
        proxy: str,
        data_dir: str,
        replica_proxy: str,
        replica_data_dir: str,
        token: str | None = None,
        replica_sync_mode: bool | None = None,
        enable: bool | None = None,
    ):
        cdef TString c_proxy = proxy.encode()
        cdef TString c_data_dir = data_dir.encode()
        cdef TString c_replica_proxy = replica_proxy.encode()
        cdef TString c_replica_data_dir = replica_data_dir.encode()
        cdef bool c_replica_sync_mode
        cdef bool c_enable
        cdef TYtStore2.TModifyReplicaOptions options
        if token:
            options.Token = token.encode()
        options.Action = TYtStore2.TModifyReplicaOptions.EAction.CREATE
        if replica_sync_mode is not None:
            # Cython cannot cast python object to the std::optional
            c_replica_sync_mode = replica_sync_mode
            options.SyncMode = c_replica_sync_mode
        if enable is not None:
            # Cython cannot cast python object to the std::optional
            c_enable = enable
            options.Enable = c_enable
        TYtStore2.ModifyReplica(c_proxy, c_data_dir, c_replica_proxy, c_replica_data_dir, options)

    @staticmethod
    def remove_replica(
        proxy: str,
        data_dir: str,
        replica_proxy: str,
        replica_data_dir: str,
        token: str | None = None,
    ):
        cdef TString c_proxy = proxy.encode()
        cdef TString c_data_dir = data_dir.encode()
        cdef TString c_replica_proxy = replica_proxy.encode()
        cdef TString c_replica_data_dir = replica_data_dir.encode()
        cdef TYtStore2.TModifyReplicaOptions options
        if token:
            options.Token = token.encode()
        options.Action = TYtStore2.TModifyReplicaOptions.EAction.REMOVE
        TYtStore2.ModifyReplica(c_proxy, c_data_dir, c_replica_proxy, c_replica_data_dir, options)

    @staticmethod
    cdef TDuration _as_duration(float seconds) noexcept:
        return TDuration.MicroSeconds(int(seconds * 1_000_000))

    def _on_disable_callback(self, err_type, msg):
        labels = {
            "error_type": err_type,
            "yt_proxy": self._proxy,
            "yt_dir": self._data_dir,
            "is_heater": self._is_heater,
        }

        try:
            import app_ctx

            if hasattr(app_ctx, 'metrics_reporter'):
                app_ctx.metrics_reporter.report_metric(
                    core_monitoring.MetricNames.YT_CACHE_ERROR,
                    labels=labels,
                    urgent=True,
                    report_type=report.ReportTypes.YT_CACHE_METRICS,
                )
        except Exception:
            logger.debug('Failed to report yt cache error metric to snowden', exc_info=True)

        labels['error'] = msg
        labels['user'] = core_config.get_user()

        report.telemetry.report(
            report.ReportTypes.YT_CACHE_ERROR,
            labels,
        )

    def _at_exit(self):
        self._store_ptr.Shutdown();
        self._exiting = True
