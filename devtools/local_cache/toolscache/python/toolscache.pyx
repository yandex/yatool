import atexit
import exts.func
import grpc
import threading

from devtools.local_cache.toolscache.proto import tools_pb2, tools_pb2_grpc
from devtools.local_cache.psingleton.proto import known_service_pb2, known_service_pb2_grpc
from devtools.local_cache.psingleton.python.systemptr import get_my_name


class _State:
    _STUB = None
    _STUB_SERVER = None
    _ADDRESS = None
    _LOCK = threading.Lock()
    _CHANNEL = None


def _get_client(paddress, server=False):
    if _State._ADDRESS == paddress:
        return _State._STUB_SERVER if server else _State._STUB

    if _State._CHANNEL is not None:
        _State._CHANNEL.close()

    _, _, address = paddress

    _State._CHANNEL = grpc.insecure_channel(address)
    _State._STUB = tools_pb2_grpc.TToolsCacheStub(_State._CHANNEL)
    _State._STUB_SERVER = known_service_pb2_grpc.TUserServiceStub(_State._CHANNEL)
    _State._ADDRESS = paddress
    return _State._STUB_SERVER if server else _State._STUB


@atexit.register
def _cleanup():
    if _State._CHANNEL is not None:
        _State._CHANNEL.close()


@exts.func.memoize()
def _memoize_my_name():
    pid, stime = get_my_name()
    return known_service_pb2.TProc(Pid=pid, StartTime=stime)


def _check_common_args(paddress, timeout, wait_for_ready):
    pid, ctime, address = paddress
    assert isinstance(pid, int), type(pid)
    assert isinstance(ctime, int), type(ctime)
    assert isinstance(address, str), type(address)

    assert timeout is None or isinstance(timeout, (int, float)), type(timeout)
    assert isinstance(wait_for_ready, bool), type(wait_for_ready)


def notify_resource_used(paddress, sbpath, sbid, pattern="", bottle="", timeout=None, wait_for_ready=False):
    _check_common_args(paddress, timeout, wait_for_ready)
    pid, ctime, address = paddress
    assert isinstance(sbpath, str)
    assert isinstance(pattern, str)
    assert isinstance(sbid, str)
    assert isinstance(bottle, str)

    query = tools_pb2.TResourceUsed(
        Peer=known_service_pb2.TPeer(
            Proc=_memoize_my_name()
        ),
        Pattern=pattern,
        Bottle=bottle,
        Resource=tools_pb2.TSBResource(
            Path=sbpath,
            SBId=sbid
        )
    )
    if not timeout:
        wait_for_ready = False

    with _State._LOCK:
        return _get_client(paddress).Notify(query, timeout=timeout, wait_for_ready=wait_for_ready)


def force_gc(paddress, disk_limit, timeout=None, wait_for_ready=False):
    _check_common_args(paddress, timeout, wait_for_ready)
    pid, ctime, address = paddress
    assert isinstance(disk_limit, int)

    query = tools_pb2.TForceGC(
        Peer=known_service_pb2.TPeer(
            Proc=_memoize_my_name()
        ),
        TargetSize=disk_limit
    )

    if not timeout:
        wait_for_ready = False

    with _State._LOCK:
        return _get_client(paddress).ForceGC(query, timeout=timeout, wait_for_ready=wait_for_ready)


def lock_resource(paddress, sbpath, sbid, timeout=None, wait_for_ready=False):
    _check_common_args(paddress, timeout, wait_for_ready)
    pid, ctime, address = paddress

    query = tools_pb2.TSBResource(
        Path=sbpath,
        SBId=sbid
    )
    if not timeout:
        wait_for_ready = False

    with _State._LOCK:
        return _get_client(paddress).LockResource(query, timeout=timeout, wait_for_ready=wait_for_ready)


def get_task_stats(paddress, timeout=None, wait_for_ready=False):
    _check_common_args(paddress, timeout, wait_for_ready)
    pid, ctime, address = paddress

    query = known_service_pb2.TPeer(
        Proc=_memoize_my_name()
    )
    if not timeout:
        wait_for_ready = False

    with _State._LOCK:
        return _get_client(paddress).GetTaskStats(query, timeout=timeout, wait_for_ready=wait_for_ready)


def check_status(paddress, timeout=None, wait_for_ready=False):
    _check_common_args(paddress, timeout, wait_for_ready)
    pid, ctime, address = paddress

    query = known_service_pb2.TPeer(
        Proc=_memoize_my_name()
    )

    if not timeout:
        wait_for_ready = False

    with _State._LOCK:
        return _get_client(paddress, server=True).GetStatus(query, timeout=timeout, wait_for_ready=wait_for_ready)


def unlock_all(paddress, timeout=None, wait_for_ready=False):
    _check_common_args(paddress, timeout, wait_for_ready)
    pid, ctime, address = paddress

    query = known_service_pb2.TPeer(
        Proc=_memoize_my_name()
    )

    if not timeout:
        wait_for_ready = False

    with _State._LOCK:
        return _get_client(paddress).UnlockAllResources(query, timeout=timeout, wait_for_ready=wait_for_ready)
