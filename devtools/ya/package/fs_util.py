"""Some extra methods and classes for ya package, that are not in exts/fs.py"""

import os
import random
import stat
import errno
import shutil

import exts.fs


def _temp_path(path):
    return path + '.tmp.' + str(random.random())


def cleanup(path, force=False):
    if force:
        os.chmod(path, stat.S_IRWXU)
        for root, dirs, _ in os.walk(path):
            for d in dirs:
                os.chmod(os.path.join(root, d), stat.S_IRWXU)

    exts.fs.ensure_removed(path)


def copy_tree(source, destination):
    def copy_function_with_follback_on_dirs(src, dst):
        try:
            return shutil.copy2(src, dst)
        except IOError as e:
            if e.errno == errno.EISDIR:
                return exts.fs.copytree3(src, dst)
            raise

    exts.fs.copytree3(source, destination, copy_function=copy_function_with_follback_on_dirs)


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
