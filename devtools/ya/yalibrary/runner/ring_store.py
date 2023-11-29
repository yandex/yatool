import os
import shutil
import logging
import time

import exts.fs
from yalibrary.runner import uid_store


class RingStore(object):
    def __init__(self, store_path, rmtree=shutil.rmtree, step=24 * 60 * 60, limit=5, main_store=None):
        if main_store is None:
            main_store = int(time.time() / step)

        exts.fs.create_dirs(store_path)
        store_dirs = os.listdir(store_path)
        current, to_delete = self._uid_store_dirs(store_dirs, main_store, limit)

        def abs_path(d):
            return os.path.join(store_path, str(d))

        for d in to_delete:
            rmtree(abs_path(d))

        self._store_path = store_path
        self._ring = [uid_store.UidStore(abs_path(d)) for d in current]

    @staticmethod
    def _uid_store_dirs(dir_list, main_store, limit):
        latest = int(main_store)
        previous = [int(x) for x in dir_list if x.isdigit() and int(x) < latest]
        current = sorted([latest] + previous, reverse=True)[:limit]
        to_delete = set(previous) - set(current)

        return current, to_delete

    def _main_store(self):
        return self._ring[0]

    def put(self, uid, root_dir, files, codec=None):
        self._main_store().put(uid, root_dir, files)

    def _find_store(self, uid):
        # main store first
        for r in self._ring:
            if r.has(uid):
                return r

    def has(self, uid):
        res = self._find_store(uid) is not None
        logging.debug('Probing %s => %s', uid, res)
        return res

    def _try_restore(self, uid, into_dir):
        r = self._find_store(uid)

        if not r:
            return False

        res = r.try_restore_x(uid, into_dir)

        if res is None:
            return False

        if r is not self._main_store():
            self.put(uid, into_dir, res)
        else:
            self._main_store().touch(uid)

        return True

    def try_restore(self, uid, into_dir):
        res = self._try_restore(uid, into_dir)
        logging.debug('Try restore %s, %s => %s', uid, into_dir, res)
        return res

    def flush(self):
        self._main_store().flush()

    def analyze(self, display):
        self._main_store().analyze(display)

    def strip(self, uids_filter):
        for r in self._ring:
            r.strip(uids_filter)

    def clear_uid(self, uid):
        for r in self._ring:
            r.clear_uid(uid)
