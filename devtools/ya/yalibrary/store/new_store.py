import collections
import six

if six.PY3:
    import contextlib
else:
    import contextlib2 as contextlib
import exts.yjson as json
import os
import logging
import time

import core.report
from exts import fs
from exts.timer import AccumulateTime

import yalibrary.store.file_store as file_store
import yalibrary.store.lru as lru
import yalibrary.store.size_store as size_store

logger = logging.getLogger(__name__)


class StopSieve(Exception):
    pass


class ItemType(object):
    UID = 'U'
    HASH = 'H'


# Cannot import UidStoreItemInfo
class NewStoreItemInfo(object):
    def __init__(self, uid, timestamp, size):
        self.uid = uid
        self.timestamp = timestamp
        self.size = size


class NewStore(object):
    def __init__(self, store_path):
        def touch_finalizer(stamp, key):
            """'touch'es HASHes left over from 'has'. See _get_file_info"""

            if not key.startswith(ItemType.UID):
                logger.debug('Unknown prefix %s', key)

            uid = key[1:]
            try:
                with contextlib.suppress(file_store.NotInCacheError):
                    for rel_path, file_info in self._get_file_info(uid):
                        self._lru.touch(ItemType.HASH + file_info['hash'])
            except Exception as e:
                logger.warning('"propagate_updates" check failed for uid %s with %s', key[1:], e)

            return 'P' + key[0], key[1:]

        self._file_store = file_store.Store(os.path.join(store_path, 'blob'))
        self._uid_store = file_store.Store(os.path.join(store_path, 'uid'))
        self._lru = lru.LruQueue(os.path.join(store_path, 'lru'), touch_finalizer)
        self._size_store = size_store.SizeStore(os.path.join(store_path, 'size'))
        self._store_path = store_path
        logger.debug('Initialized store in %s', self._store_path)

        self.timers = {'has': 0, 'put': 0, 'get': 0, 'remove': 0}
        self.counters = {'has': 0, 'put': 0, 'get': 0, 'remove': 0}
        self.failures = {'has': 0, 'put': 0, 'get': 0, 'remove': 0}

    def _inc_time(self, x, tag):
        self.timers[tag] += x
        self.counters[tag] += 1

    def _count_failure(self, tag):
        self.failures[tag] += 1

    def put(self, uid, root_dir, files, codec=None):
        with AccumulateTime(lambda x: self._inc_time(x, 'put')):
            file_map = {}
            try:
                for x in files:
                    mode = os.stat(x).st_mode
                    x_new, h, size = self._file_store.put_file(x, codec)
                    self._lru.touch(ItemType.HASH + h)
                    fsize = self._get_fs_file_size(x_new)
                    self._size_store[h] = fsize
                    file_map[os.path.relpath(x, root_dir)] = {
                        'hash': h,
                        'size': size,
                        'codec': codec,
                        'mode': mode,
                        'fsize': fsize,
                    }

                content = json.dumps({'files': file_map, 'uid': uid})
                self._lru.touch(ItemType.UID + uid)
                self._uid_store.put(uid, content)
                logger.debug('Store %s for %s', content, uid)
            except Exception as e:
                self._count_failure('put')
                logger.exception('Error (%s) storing %s(%s, %s)', e, uid, list(files), file_map)

    def has(self, uid):
        with AccumulateTime(lambda x: self._inc_time(x, 'has')):
            res = False

            if self._uid_store.has(uid):
                # Postpone complete update
                self._lru.touch(ItemType.UID + uid, update_queue=True)
                res = True

            # logging is too slow
            # logger.debug('Probing %s => %s', uid, res)

            return res

    @staticmethod
    def _rollback_if_need(uid, paths):
        if paths:
            logger.debug('Rolling back partially restored %s, restored files: %s', uid, paths)
            for path in paths:
                try:
                    os.unlink(path)
                except Exception as e:
                    logger.debug('Cannot remove %s: %s', path, e)

    def try_restore(self, uid, into_dir):
        with AccumulateTime(lambda x: self._inc_time(x, 'get')):
            to_clean = []
            try:
                for rel_path, file_info in self._get_file_info(uid):
                    self._lru.touch(ItemType.HASH + file_info['hash'])

                    path = os.path.join(into_dir, rel_path)
                    path_in_storage = self._file_store.extract_file(
                        file_info['hash'], path, file_info['codec'], mode=file_info['mode']
                    )
                    to_clean.append(path)
                    file_size = fs.get_file_size(path_in_storage)
                    if file_size != file_info['size']:
                        logger.debug('Incorrect file size in store, fname=%s', path)
                        return False
                    if 'fsize' not in file_info:
                        self._size_store[file_info['hash']] = self._get_fs_file_size(path_in_storage)

                logger.debug('Restoration of %s into %s succeed', uid, into_dir)
                del to_clean[:]
            except file_store.NotInCacheError:
                self._count_failure('get')
                return False
            except Exception as e:
                logger.debug('Restoration of %s into %s failed with %s', uid, into_dir, e)
                self._count_failure('get')
                return False
            finally:
                self._lru.touch(ItemType.UID + uid)
                self._rollback_if_need(uid, to_clean)

            return True

    def _get_fs_file_size(self, path):
        s = os.lstat(path) if os.path.islink(path) else os.stat(path)

        if hasattr(s, 'st_blocks'):
            return s.st_blocks * 512

        return s.st_size

    def _get_file_info(self, uid):
        try:
            raw_data = self._uid_store.get(uid)
            try:
                meta = json.loads(raw_data)
            except ValueError:
                raise file_store.NotInCacheError('Cannot decode metadata for {}'.format(uid))

            for rel_path, file_info in six.iteritems(meta['files']):
                yield rel_path, file_info

        except file_store.NotInCacheError:
            logger.debug('File info of uid %s is missing', uid)
            raise

    def _get_file_stats(self, uid, no_touch=False):
        # Does not update lru.
        try:
            raw_data = self._uid_store.get(uid)
            try:
                meta = json.loads(raw_data)
            except ValueError:
                raise file_store.NotInCacheError('Cannot decode metadata for {}'.format(uid))

            for rel_path, file_info in six.iteritems(meta['files']):
                yield rel_path, file_info

        except file_store.NotInCacheError:
            logger.debug('File info of uid %s is missing', uid)
            raise

    def sieve(self, stopper, state):
        def remover(stamp, key):
            if stopper(stamp):
                logger.debug('Stop sieve on stamp %d', stamp)
                raise StopSieve
            if key.startswith(ItemType.UID):
                self._uid_store.remove(key[1:])
                logger.debug('Removed %s / %d from uid store', key[1:], stamp)
            elif key.startswith(ItemType.HASH):
                self._file_store.remove(key[1:])
                del self._size_store[key[1:]]
                logger.debug('Removed %s / %d from file store', key[1:], stamp)
            else:
                logger.debug('Unknown prefix %s', key)
                return 'E', key
            return key[0], key[1:]

        try:
            for x in self._lru.sieve(remover):
                state.check_cancel_state()
                yield x
        except StopSieve:
            return

    def analyze(self, display):
        # filename -> total size consumed
        sz = collections.Counter()
        # filename -> number of occurrences
        freq = collections.Counter()

        def analyzer(stamp, key):
            if key.startswith(ItemType.UID):
                with contextlib.suppress(file_store.NotInCacheError):
                    for rel_path, file_info in self._get_file_stats(key[1:]):
                        sz[rel_path] += file_info.get('fsize', file_info.get('size'))
                        freq[rel_path] += 1
                return key[0], key[1:]

        for i in self._lru.analyze(analyzer):
            pass

        for item in sz.most_common(100):
            name, size = item[0], item[1]
            display.emit_message('{:10} {:5} - {}'.format(size, freq[name], name))

    def size(self):
        return self._size_store.size()

    def compact(self, interval, max_cache_size, state):
        now = int(time.time())

        def stopper(stamp):
            if stamp <= now - interval:
                return False
            if max_cache_size is not None and self._size_store.size() > max_cache_size:
                return False
            return True

        return list(self.sieve(stopper, state))

    def convert(self, converter, state):
        """
        Move data from the cache to another
        """

        def mover(stamp, key):
            state.check_cancel_state()
            if key.startswith(ItemType.UID):
                try:
                    info = {}
                    codec = None
                    for rel_path, file_info in self._get_file_info(key[1:]):
                        fhash = file_info['hash']
                        info[rel_path] = (self._file_store._discriminant(fhash), file_info['mode'], fhash)
                        del self._size_store[fhash]
                        codec = codec or file_info.get('codec')

                    if not codec:
                        converter(key[1:], info)
                    self.clear_uid(key[1:])
                except file_store.NotInCacheError:
                    pass
            elif key.startswith(ItemType.HASH):
                pass
            else:
                return 'E', key
            return key[0], key[1:]

        list(self._lru.sieve(mover))
        self.flush()
        fs.remove_tree_safe(self._store_path)

    def clear_tray(self):
        self._file_store.clear_tray()
        self._uid_store.clear_tray()

    # Need external synchronization. Suitable for garbage collection.
    # Too slow for rebuild cleanup.
    def strip(self, uids_item_info_filter):
        self.clear_tray()
        used_uids = set()
        used_file_uids = set()
        file_uids_to_remove = set()

        def uids_filter(stamp, key):
            # HASHes are stripped from LRU using regular compact process
            if key.startswith(ItemType.HASH):
                return True

            if not key.startswith(ItemType.UID):
                return False

            total_size = 0
            file_uids = set()
            try:
                for rel_path, file_info in self._get_file_stats(key[1:]):
                    h = file_info['hash']
                    s = file_info['size']
                    total_size += s
                    file_uids.add(h)
                    self._size_store[h] = s

                # uids_filter expects UidStoreItemInfo
                if uids_item_info_filter(NewStoreItemInfo(key[1:], stamp, total_size)):
                    used_file_uids.update(file_uids)
                    used_uids.add(key[1:])
                    return True
                else:
                    file_uids_to_remove.update(file_uids)
                    return False

            except file_store.NotInCacheError:
                return False

        logger.debug('Started filtering')
        self._lru.strip(uids_filter)
        logger.debug('Done filtering, retain %d uids, %d files', len(used_uids), len(used_file_uids))

        logger.debug("Cleaning size store")
        for file_uid in file_uids_to_remove:
            del self._size_store[file_uid]

        logger.debug("Cleaning file store")
        self._file_store.gc(used_file_uids)

        logger.debug("Cleaning uid store")
        self._uid_store.gc(used_uids)

    def flush(self):
        self._size_store.flush()
        self._lru.flush()

    def clear_uid(self, uid):
        with AccumulateTime(lambda x: self._inc_time(x, 'remove')):
            # lru is shared by both UIDs and HASHes.
            # HASHes should be collected separately in compact using _lru.
            self._uid_store.remove(uid)

    def stats(self, execution_log):
        for k, v in six.iteritems(self.timers):
            stat_dict = {
                'timing': (0, v),
                'total_time': True,
                'count': self.counters[k],
                'prepare': '',
                'type': 'new-store',
                'failures': self.failures[k],
            }
            core.report.telemetry.report('new_store_stats-{}'.format(k), stat_dict)
            execution_log["$(new-store-{})".format(k)] = stat_dict
