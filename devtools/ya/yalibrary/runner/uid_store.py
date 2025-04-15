import os
import time
import logging
import collections

import six

import exts.fs

from yalibrary.store import file_store
import yalibrary.runner.fs

from yalibrary.runner import lru_store

logger = logging.getLogger(__name__)


def _strip_path(root_dir, file_path):
    root_dir_wsuf = root_dir + os.path.sep
    if not file_path.startswith(root_dir_wsuf):
        raise Exception('{} must contains in {}'.format(file_path, root_dir))
    return file_path.replace(root_dir_wsuf, '')


class UidStoreItemInfo(object):
    def __init__(self, uid, paths, timestamp):
        self.uid = uid
        self.timestamp = timestamp
        self.paths = paths

    @property
    def size(self):
        size = 0
        for path in self.paths:
            size += exts.fs.get_file_size(path)
        return size


class UidStore(object):
    def __init__(self, store_path):
        self._store_path = store_path
        store_path = os.path.join(store_path, 'v1')

        exts.fs.create_dirs(store_path)

        self._file_store = file_store.FileStore(store_path)
        self._lru_store = lru_store.LruStore(store_path)

    def put(self, uid, root_dir, files, codec=None):
        root_dir = os.path.abspath(root_dir)
        files = [os.path.abspath(x) for x in files]

        kv = dict((_strip_path(root_dir, x), self._file_store.add_file(x)[0]) for x in files)
        self._lru_store.put(uid, kv, time.time())

    def touch(self, uid):
        self._lru_store.touch(uid, time.time())

    def has(self, uid):
        return self._lru_store.has(uid)

    def try_restore(self, uid, into_dir):
        return self.try_restore_x(uid, into_dir) is not None

    def try_restore_x(self, uid, into_dir):
        kv = self._lru_store.try_extract(uid)

        if kv is None:
            return None

        ret = []

        try:
            for rel_path, file_hash in six.iteritems(kv):
                f_path = os.path.join(into_dir, rel_path)

                yalibrary.runner.fs.make_hardlink(self._file_store.get(file_hash), f_path)
                ret.append(f_path)
        except file_store.NotInCacheError as e:
            logger.error(
                "Could not restore some files from cache: %s"
                "It likely means that the cache has been corrupted. Run 'ya gc cache --age-limit 0' to drop the cache.",
                e,
            )
            raise

        return ret

    def flush(self):
        self._lru_store.flush()

    def analyze(self, display):
        sz = collections.Counter()
        qty = collections.Counter()

        for k, v in six.iteritems(self._lru_store.data):
            for f in v:
                sz[f[0]] += os.path.getsize(self._file_store.get(f[1]))
                qty[f[0]] += 1

        for item in sz.most_common(100):
            name, size = item[0], item[1]
            display.emit_message('{:10} {:5} - {}'.format(size, qty[name], name))

    def strip(self, uids_filter):
        def item_timestamp(item):
            return max([z[2] for z in item])

        uids_to_remove = []
        used_file_uids = set()
        file_uids_to_remove = set()
        for uid, files in sorted(self._lru_store.data.items(), key=lambda x: item_timestamp(x[1]), reverse=True):

            def get(file):
                try:
                    return self._file_store.get(file)
                except file_store.NotInCacheError:
                    return None

            paths = [x for x in [get(e[1]) for e in files] if x is not None]
            file_uids = set(self._lru_store.try_extract(uid).values())
            if not uids_filter(UidStoreItemInfo(uid, paths, item_timestamp(files))):
                logger.debug('Uid %s will be removed from the cache', uid)
                file_uids_to_remove |= file_uids
                uids_to_remove.append(uid)
            else:
                used_file_uids |= file_uids

        for uid in uids_to_remove:
            self._lru_store.remove_uid(uid)
        self._lru_store.flush()

        for file_uid in file_uids_to_remove:
            if file_uid not in used_file_uids:
                self._file_store.remove_uid(file_uid)

    def clear_uid(self, uid):
        vals = self._lru_store.try_extract(uid)
        if vals:
            for file_uid in vals.values():
                self._file_store.remove_uid(file_uid)

        self._lru_store.remove_uid(uid)
