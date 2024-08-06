import logging
import os
import stat
import threading
import time

import six

import exts.fs
import exts.hashing
import exts.path2

import yalibrary.runner.fs

from exts import fs


AUX_EXT = '.link'

logger = logging.getLogger(__name__)


class ResultStore(object):
    _LOCK = threading.Lock()
    _DESTINATIONS = set()

    def __init__(self, root):
        self.root = os.path.abspath(root)
        yalibrary.runner.fs.prepare_dir(self.root)

    def _put_with_path_transform(
        self, target, path, action, transform_path, forced_existing_dir_removal=False, tared=False
    ):
        path = self.normpath(path)
        output_path = os.path.join(self.root, transform_path(path))

        with self._LOCK:
            if output_path in self._DESTINATIONS:
                logger.debug('Skipping result processing for output_path %s, it is duplicated', output_path)
                return output_path
            self._DESTINATIONS.add(output_path)

            if not yalibrary.runner.fs.prepare_parent_dir(output_path):
                return output_path

            if forced_existing_dir_removal or not os.path.isdir(output_path) or os.path.islink(output_path):
                fs.ensure_removed(output_path)

        action(target, output_path, tared)
        return output_path

    def put(
        self,
        target,
        path,
        action=yalibrary.runner.fs.make_hardlink,
        forced_existing_dir_removal=False,
        tared=False,
        tared_nodir=False,
    ):
        output_path = self._put_with_path_transform(
            target,
            path,
            action,
            transform_path=lambda p: p,
            forced_existing_dir_removal=forced_existing_dir_removal,
            tared=False if tared and tared_nodir else tared,
        )
        if tared and tared_nodir:
            action(target, os.path.join(self.root, self.normpath(os.path.dirname(path))), True)
        return output_path

    @staticmethod
    def normpath(path):
        path = path.lstrip('/')
        path = path.lstrip(os.sep)
        return os.path.normpath(path)


class SymlinkResultStore(ResultStore):
    def __init__(self, root, src_root):
        super(SymlinkResultStore, self).__init__(root)
        self.src_root = os.path.abspath(src_root)
        if not os.path.isdir(self.src_root):
            exts.fs.create_dirs(self.src_root)
        self.symlinks = {}
        self.file_lock_name = os.path.join(root, 'consume.lock')

    def put(
        self,
        target,
        path,
        target_action=yalibrary.runner.fs.make_hardlink,
        forced_existing_dir_removal=False,
        tared=False,
        tared_nodir=False,
    ):
        path = self.normpath(path)
        symlink_path = os.path.join(self.src_root, path)
        symlinkable_path = self._put_with_path_transform(
            target,
            symlink_path,
            action=target_action,
            transform_path=lambda p: self.symlinkable(p),
            forced_existing_dir_removal=forced_existing_dir_removal,
            tared=False if tared and tared_nodir else tared,
        )
        self._log_link(symlinkable_path, symlink_path)
        self.symlinks[path] = symlinkable_path
        if tared and tared_nodir:
            symlinkable_path_unpacked = self._put_with_path_transform(
                target,
                symlink_path + '_unpacked',
                action=target_action,
                transform_path=lambda p: self.symlinkable(p),
                forced_existing_dir_removal=False,
                tared=True,
            )
            if os.path.isdir(symlinkable_path_unpacked):
                path_dir = os.path.dirname(path)
                symlink_path_dir = os.path.dirname(symlinkable_path_unpacked)
                for f in os.listdir(symlinkable_path_unpacked):
                    f_path = os.path.join(symlinkable_path_unpacked, f)
                    if os.path.isfile(f_path) or os.path.isdir(f_path):
                        f_symlink_path = os.path.join(symlink_path_dir, f)
                        self._log_link(f_path, f_symlink_path)
                        self.symlinks[os.path.join(path_dir, f)] = f_path
        return symlink_path

    def symlinkable(self, symlink_path):
        return os.path.join(exts.hashing.md5_value(symlink_path), os.path.basename(symlink_path))

    def commit(self):
        for path, target in six.iteritems(self.symlinks):
            if self.prepare_src_dir(path):
                yalibrary.runner.fs.make_symlink(
                    target, os.path.join(self.src_root, path), warn_is_file=True, compare_file=True
                )

    def prepare_src_dir(self, path):
        dir_path = os.path.dirname(path)
        dir_symlink_path = os.path.join(self.src_root, dir_path)
        if not dir_path or os.path.isdir(dir_symlink_path):
            return True

        with self._LOCK:
            for prefix in exts.path2.path_prefixes(dir_path):
                dir_symlink_path = os.path.join(self.src_root, prefix)
                if os.path.islink(dir_symlink_path):
                    dir_link_target = os.readlink(dir_symlink_path)
                    if exts.path2.path_startswith(dir_link_target, self.root):
                        if not os.path.exists(dir_link_target):
                            exts.fs.ensure_dir(dir_link_target)
                        return True
                    else:
                        logger.debug(
                            "Cannot create parent directory %s for target %s, existing link points to %s",
                            dir_symlink_path,
                            path,
                            dir_link_target,
                        )
                        return False
                if not os.path.isdir(dir_symlink_path):
                    break

            dir_symlinkable_path = os.path.join(self.root, self.symlinkable(dir_symlink_path))

            if dir_symlinkable_path not in self._DESTINATIONS:
                exts.fs.ensure_removed(dir_symlinkable_path)
                exts.fs.create_dirs(dir_symlinkable_path)
                self._DESTINATIONS.add(dir_symlinkable_path)

            if dir_symlink_path not in self._DESTINATIONS:
                yalibrary.runner.fs.make_symlink(dir_symlinkable_path, dir_symlink_path)
                self._log_link(dir_symlinkable_path, dir_symlink_path)
                self._DESTINATIONS.add(dir_symlink_path)

            return True

    def _log_link(self, symlinkable_path, symlink_path):
        with open(os.path.dirname(symlinkable_path) + AUX_EXT, 'w') as f:
            f.write(symlink_path)

    @staticmethod
    def _is_symres_link(link, item):
        return os.path.islink(link) and os.path.dirname(os.path.realpath(os.readlink(link))) == os.path.realpath(item)

    def _remove_item(self, item):
        link_file = item + AUX_EXT
        if os.path.exists(link_file):
            with open(link_file, 'r') as f:
                link = f.read()

            if self._is_symres_link(link):
                logger.debug("Removing symlink %s", link)
                fs.remove_tree_safe(link)

        logger.debug("Removing link %s", item)
        if os.path.isdir(item):
            os.chmod(item, stat.S_IRWXU)
            for path, dirs, _ in os.walk(item):
                for momo in dirs:
                    os.chmod(os.path.join(path, momo), stat.S_IRWXU)
        fs.remove_tree_safe(item)

    def sieve(self, state, ttl, cleanup=False):
        min_mtime = time.time() - ttl

        for base_name in os.listdir(self.root):
            item = os.path.join(self.root, base_name)

            try:
                if not item.endswith(AUX_EXT) and (cleanup or os.stat(item).st_mtime < min_mtime):
                    self._remove_item(item)
            except Exception as e:
                logger.debug("Failed to remove %s: %s", item, str(e))

            if state:
                state.check_cancel_state()


# XXX: Legacy (DEVTOOLS-1128)
class LegacyInstallResultStore(ResultStore):
    def __init__(self, root):
        super(LegacyInstallResultStore, self).__init__(root)

    def put(self, target, path, action=yalibrary.runner.fs.make_hardlink, forced_existing_dir_removal=False):
        return self._put_with_path_transform(
            target,
            path,
            action,
            transform_path=lambda p: os.path.basename(p),
            forced_existing_dir_removal=forced_existing_dir_removal,
        )
