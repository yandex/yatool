# cython: c_string_type=unicode, c_string_encoding=utf8

from libcpp cimport bool as cbool
from util.generic.string cimport TString
from util.generic.vector cimport TVector
from posix.types cimport pid_t, time_t

import atexit
import grpc
import logging

from devtools.local_cache.ac.proto import ac_pb2


logger = logging.getLogger(__name__)


cdef extern from "devtools/local_cache/ac/python/ac.h" namespace "NAcClient":
    void InitGlobals(object acPb2Module, object _logger, object grpcErrorCls)
    void ClearGlobals()
    void ExceptionHandler()

    # simulate std::tuple as struct
    cppclass TAddress:
        TAddress()
        TAddress(pid_t, time_t, TString)

    cppclass TBlob:
        TBlob()
        TBlob(TString, TString)

    object PutUid(
        const TAddress& paddress,
        const TString& uid,
        const TString& rootPath,
        const TVector[TBlob]& blobs,
        long long weight,
        cbool hardlink,
        cbool replace,
        cbool isResult,
        const TVector[TString]& fileNames,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object GetUid(
        const TAddress& paddress,
        const TString& uid,
        const TString& destPath,
        cbool hardlink,
        cbool isResult,
        cbool release,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object HasUid(
        const TAddress& paddress,
        const TString& uid,
        cbool isResult,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object RemoveUid(
        const TAddress& paddress,
        const TString& uid,
        cbool forcedRemoval,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object GetTaskStats(
        const TAddress& paddress,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object PutDeps(
        const TAddress& paddress,
        const TString& uid,
        const TVector[TString]& deps,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object ForceGC(
        const TAddress& paddress,
        unsigned long long diskLimit,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object ReleaseAll(
        const TAddress& paddress,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object GetCacheStats(
        const TAddress& paddress,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object AnalyzeDU(
        const TAddress& paddress,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler

    object SynchronousGC(
        const TAddress& paddress,
        long long totalSize,
        long long minLastAccess,
        long long maxObjectSize,
        double timeout,
        cbool waitForReady
    ) except +ExceptionHandler


# Trivial replacement of python grpc exception
class GrpcError(grpc.RpcError):
    def __init__(self, code: int, details: str):
        from grpc._common import CYGRPC_STATUS_CODE_TO_STATUS_CODE
        self._code = CYGRPC_STATUS_CODE_TO_STATUS_CODE[code]
        self._details = details

    def code(self) -> grpc.StatusCode:
        return self._code

    def details(self) -> str:
        return self._details

    def __str__(self) -> str:
        return repr(self)

    def __repr__(self) -> str:
        return "code={}, details={}".format(self._code, self._details)


# To reduce boilerplate in cpp code pass some usefull python objects
InitGlobals(ac_pb2, logger, GrpcError)
atexit.register(ClearGlobals)

cdef TAddress get_address(paddress):
    pid, ctime, address = paddress
    return TAddress(pid, ctime, address)


def put_uid(
    paddress: tuple[int, int, str],
    uid: str,
    root_path: str,
    blobs: list[tuple[str, str|None]],
    weight: int,
    hardlink: bool,
    replace: bool,
    is_result:bool,
    file_names: list[str]|None = None,
    timeout: float|None = None,
    wait_for_ready: bool = False
):
    assert file_names is None or len(file_names) == len(blobs), (len(file_names), len(blobs))

    cdef TVector[TBlob] c_blobs
    for fpath, fuid in blobs:
        c_blobs.push_back(TBlob(fpath, fuid or ""))

    return PutUid(
        get_address(paddress),
        uid,
        root_path,
        c_blobs,
        weight,
        hardlink,
        replace,
        is_result,
        file_names if file_names else [],
        timeout or 0.0,
        wait_for_ready
    )


def get_uid(
    paddress: tuple[int, int, str],
    uid: str,
    dest_path: str,
    hardlink: bool,
    is_result: bool,
    release: bool = False,
    timeout: float|None = None,
    wait_for_ready: bool = False
):
    return GetUid(
        get_address(paddress),
        uid,
        dest_path,
        hardlink,
        is_result,
        release,
        timeout or 0.0,
        wait_for_ready
    )


def has_uid(paddress: tuple[int, int, str], uid: str, is_result: bool, timeout: float|None = None, wait_for_ready: bool = False):
    return HasUid(
        get_address(paddress),
        uid,
        is_result,
        timeout or 0.0,
        wait_for_ready
    )


def remove_uid(paddress: tuple[int, int, str], uid: str, forced_removal: bool = False, timeout: float|None = None, wait_for_ready: bool = False):
    return RemoveUid(
        get_address(paddress),
        uid,
        forced_removal,
        timeout or 0.0,
        wait_for_ready
    )


def get_task_stats(paddress: tuple[int, int, str], timeout: float|None = None, wait_for_ready: bool = False):
    return GetTaskStats(
        get_address(paddress),
        timeout or 0.0,
        wait_for_ready
    )


def put_dependencies(paddress: tuple[int, int, str], uid: str, deps: list[str], timeout: float|None = None, wait_for_ready: bool = False):
    return PutDeps(
        get_address(paddress),
        uid,
        deps,
        timeout or 0.0,
        wait_for_ready
    )


def force_gc(paddress: tuple[int, int, str], disk_limit: int, timeout: float|None = None, wait_for_ready: bool = False):
    return ForceGC(
        get_address(paddress),
        disk_limit,
        timeout or 0.0,
        wait_for_ready
    )


def release_all(paddress: tuple[int, int, str], timeout: float|None = None, wait_for_ready: bool = False):
    return ReleaseAll(
        get_address(paddress),
        timeout or 0.0,
        wait_for_ready
    )


def get_cache_stats(paddress: tuple[int, int, str], timeout: float|None = None, wait_for_ready: bool = False):
    return GetCacheStats(
        get_address(paddress),
        timeout or 0.0,
        wait_for_ready
    )


def analyze_du(paddress, timeout=None, wait_for_ready=False):
    return AnalyzeDU(
        get_address(paddress),
        timeout or 0.0,
        wait_for_ready
    )


def _none_as_negative(value: float|None) -> int:
    return int(value) if value is not None else -1


def synchronous_gc(
    paddress,
    total_size: float|None = None,
    min_last_access: float|None = None,
    max_object_size: float|None = None,
    timeout: float|None = None,
    wait_for_ready: bool = False
):
    assert sum(1 for x in (total_size, min_last_access, max_object_size) if x is not None) < 2, "Only one of the limits must be specified"
    return SynchronousGC(
        get_address(paddress),
        _none_as_negative(total_size),
        _none_as_negative(min_last_access),
        _none_as_negative(max_object_size),
        timeout or 0.0,
        wait_for_ready
    )
