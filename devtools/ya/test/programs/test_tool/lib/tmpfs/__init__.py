import contextlib
import os

try:
    from . import mount
    from devtools.ya.test.programs.test_tool.lib import unshare
except ImportError:
    mount = None
    unshare = None


_UNSHARED = [False]


def _is_unshared_once():
    return _UNSHARED[0]


def is_mount_supported():
    return bool(mount)


def mount_tempfs_newns(path, size_in_mb):
    if not is_mount_supported():
        raise Exception("Platform doesn't support mount within ns")

    if not _is_unshared_once():
        unshare.unshare_ns(
            unshare.CLONE_NEWNS | unshare.CLONE_NEWUSER,
            os.geteuid(),
            os.getegid(),
        )
        _UNSHARED[0] = True

    mount.mount_tmpfs(path, size_in_mb)


def unmount(path):
    if not is_mount_supported():
        raise Exception("Platform doesn't support mount within ns")
    return mount.unmount(path)


@contextlib.contextmanager
def with_tmpfs(path, size_in_mb):
    assert not os.path.exists(path)
    os.makedirs(path)
    mount_tempfs_newns(path, size_in_mb)
    yield os.path.realpath(path)
    unmount(path)
