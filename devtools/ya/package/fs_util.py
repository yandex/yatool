"""Some extra methods and classes for ya package, that are not in exts/fs.py"""

import os
import random
import stat
import errno
import shutil
import typing as tp  # noqa

import exts.fs


def _temp_path(path):
    # type: (str) -> str
    return path + '.tmp.' + str(random.random())


def cleanup(path, force=False):
    # type: (str, bool) -> None
    if force:
        os.chmod(path, stat.S_IRWXU)
        for root, dirs, _ in os.walk(path):
            for d in dirs:
                os.chmod(os.path.join(root, d), stat.S_IRWXU)

    exts.fs.ensure_removed(path)


def copy_tree(source, destination, symlinks=False, dirs_exist_ok=False):
    # type: (str, str, bool, bool) -> None

    def copy_function_with_follback_on_dirs(src, dst):
        # type: (str, str) -> None

        try:
            return shutil.copy2(src, dst)
        except IOError as e:
            if e.errno == errno.EISDIR:
                return exts.fs.copytree3(src, dst, symlinks=symlinks)
            raise

    exts.fs.copytree3(
        source,
        destination,
        copy_function=copy_function_with_follback_on_dirs,
        symlinks=symlinks,
        dirs_exist_ok=dirs_exist_ok,
    )


def hardlink_or_copy(src, lnk, copy_function=exts.fs.copy2_safe):
    # type: (str, str, tp.Callable) -> None

    try:
        return exts.fs.hardlink_or_copy(src, lnk, copy_function)
    except OSError as e:
        if e.errno == errno.EEXIST:
            same_hardlink = os.stat(src).st_ino == os.stat(lnk).st_ino
            if not same_hardlink:
                # it's faster to remove and make new hardlink then copying
                exts.fs.remove_file(lnk)
                return exts.fs.hardlink_or_copy(src, lnk, copy_function)
        else:
            raise


class AtomicPath(object):
    def __init__(self, path, remove_on_error=True, force=False):
        self.path = path
        self.remove_on_error = remove_on_error
        self.force = force
        self._path = _temp_path(path)

    def __enter__(self):
        return self._path

    def __exit__(self, type, value, traceback):
        if os.path.exists(self._path):
            if not type:
                exts.fs.move(self._path, self.path)
            elif self.remove_on_error:
                cleanup(self._path, force=self.force)
