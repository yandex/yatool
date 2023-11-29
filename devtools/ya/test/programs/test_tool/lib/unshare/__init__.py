import os

from . import identity

try:
    from library.python import nstools
except ImportError:
    nstools = None


CLONE_NEWCGROUP = 0x02000000
CLONE_NEWIPC = 0x08000000
CLONE_NEWNET = 0x40000000
CLONE_NEWNS = 0x00020000
CLONE_NEWPID = 0x20000000
CLONE_NEWUSER = 0x10000000
CLONE_NEWUTS = 0x04000000


def is_unshare_available():
    return nstools is not None


def unshare_ns(flags, mapuser=None, mapgroup=None):
    if not nstools:
        raise Exception("Platform doesn't support unshare")

    ruid, euid, suid = os.getresuid()
    rgid, egid, sgid = os.getresgid()

    nstools.unshare_ns(flags)

    if mapuser is not None:
        identity.map_id('/proc/self/uid_map', mapuser, euid)

    if mapgroup is not None:
        identity.setgroups_control(False)
        identity.map_id('/proc/self/gid_map', mapgroup, egid)
