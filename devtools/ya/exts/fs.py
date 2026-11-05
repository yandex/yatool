import logging
import os
import stat

from library.python import windows
from library.python.fs import *  # noqa

if supports_clone():  # noqa F405
    from library.python.fs.clonefile import macos_clone_file  # noqa

logger = logging.getLogger(__name__)


def remove_tree_with_perm_update(dir):
    def remove():
        try:
            ensure_removed(dir)  # noqa
            return None
        except OSError as error:
            logger.debug('Error while removing dir: %s', error)
            return error

    if remove() is None:
        return

    logger.debug('Trying to change permissions while removing dir: %s', dir)
    on_windows = windows.on_win()

    # On POSIX only directories need write permission for unlink. Files may be
    # hardlinked to cached outputs, and symlink targets must stay untouched.
    def make_writable(path):
        metadata = os.lstat(path)
        mode = stat.S_IMODE(metadata.st_mode)
        if stat.S_ISDIR(metadata.st_mode):
            os.chmod(path, mode | stat.S_IRWXU)
            return True
        if on_windows and stat.S_ISREG(metadata.st_mode):
            # Windows needs the read-only file attribute cleared before unlink.
            os.chmod(path, mode | stat.S_IWRITE)
        return False

    try:
        if make_writable(dir):
            for root, dirs, files in os.walk(dir, followlinks=False):
                for name in dirs:
                    make_writable(os.path.join(root, name))
                if on_windows:
                    for name in files:
                        make_writable(os.path.join(root, name))
    except Exception as error:
        logger.debug('Error while changing permissions: %s', error)

    error = remove()
    if error is not None:
        logger.warning('Failed to remove %s after updating permissions: %s', dir, error)


def fast_relpath(path, start):
    start = start.rstrip("/") + "/"
    if path.startswith(start):
        return path[len(start) :]
    else:
        return os.path.relpath(path, start)
