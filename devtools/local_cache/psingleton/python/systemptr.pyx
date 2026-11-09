import cython
from library.python import func 
from util.generic.maybe cimport TMaybe
from util.generic.string cimport TString
from util.system.types cimport i32, i64

cdef extern from "devtools/local_cache/psingleton/systemptr.h" nogil:
    cdef cppclass TProcessUID:
        bint CheckProcess() except +
        i32 GetPid() except +
        i64 GetStartTime() except +

    cdef cppclass TSystemWideName:
        bint CheckProcess() except +
        i32 GetPid() except +
        i64 GetStartTime() except +
        TString ToGrpcAddress() except +

    cdef enum ELockWait:
        Blocking, NonBlocking

    cdef enum ECheckLiveness:
        CheckAlive, NoCheckAlive

    cdef cppclass TPFileSingleton:
        TPFileSingleton(TString, TLog) except +

        TMaybe[TSystemWideName] GetInstanceName(ELockWait) except +

    cdef TProcessUID GetMyName "TProcessUID::GetMyName"() except +

    cdef TString GetMyUniqueSuffix "TProcessUID::GetMyUniqueSuffix"() except +

    cdef TPFileSingleton* GetClientPSingleton(TString, TString, bint)


def get_server_info(lock_file, log_file=None, non_blocking=False, fresh=False):
    cdef TString lock = lock_file.encode()

    cdef bint nb = non_blocking

    cdef bint c_fresh = fresh

    cdef TString log = log_file.encode() if log_file else ''

    cdef TPFileSingleton* info = GetClientPSingleton(lock, log, c_fresh)

    try:
        res = cython.operator.dereference(info).GetInstanceName(NonBlocking if nb else Blocking)
    finally:
        if fresh:
            del info

    if res.Empty():
        return (None, None, None)

    cdef TSystemWideName name = res.GetRef()

    return (name.GetPid(), name.GetStartTime(), name.ToGrpcAddress().decode())


@func.memoize()
def get_my_name():
    cdef TProcessUID name = GetMyName()
    return (name.GetPid(), name.GetStartTime())


def get_proc_uniq_suffix():
    cdef TString suff = GetMyUniqueSuffix()
    return suff


cdef extern from "devtools/local_cache/psingleton/systemptr-impl.h" namespace "NSystemWideName":
    i64 GetProcessCtime(i32) noexcept


def get_process_start_time(i32 pid):
    """Return native process identity units, or zero for a non-live process."""
    return GetProcessCtime(pid) if pid > 0 else 0
