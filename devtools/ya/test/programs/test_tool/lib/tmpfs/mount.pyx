cdef extern from "<sys/mount.h>" nogil:
    int mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data)
    int umount(const char *target)

from cpython.exc cimport PyErr_SetFromErrno


def mount_tmpfs(path, size_in_mb):
    path = path.encode("utf-8")
    opts = "size={}M".format(size_in_mb).encode("utf-8")
    cdef const char* opts_ptr = opts
    source = "tmpfs".encode("utf-8")
    fstype = "tmpfs".encode("utf-8")

    cdef int ret = mount(source, path, fstype, 0, <const void*>opts_ptr)
    if ret != 0:
        PyErr_SetFromErrno(OSError)


def unmount(path):
    path = path.encode("utf-8")
    cdef int ret = umount(path)
    if ret != 0:
        PyErr_SetFromErrno(OSError)
