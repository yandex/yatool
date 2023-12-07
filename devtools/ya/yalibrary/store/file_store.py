import errno
import logging
import os

from exts import fs
from exts import hashing
from exts import uniq_id

from library.python import compress


logger = logging.getLogger(__name__)


class UidNotFoundError(Exception):
    pass


class FileNotFound(Exception):
    pass


class NotInCacheError(Exception):
    pass


def hardlink(src, dst):
    try:
        fs.hardlink_or_copy(src, dst)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise e


class Store(object):
    _HEX = '0123456789abcdef'

    def __init__(self, store_path):
        self._store_path = store_path
        self._data_store = os.path.join(store_path, 'data')
        self._tray_path = os.path.join(store_path, 'tray')

        self._prepare()

    @staticmethod
    def _hash(key):
        return hashing.md5_value(key)

    def _prepare(self):
        fs.create_dirs(self._data_store)
        fs.create_dirs(self._tray_path)
        for x in self._HEX:
            for y in self._HEX:
                fs.create_dirs(os.path.join(self._data_store, x, y))

    def _discriminant(self, key):
        h = self._hash(key)
        return os.path.join(self._data_store, h[0], h[1], h)

    def _gen_tmp_path(self):
        return os.path.join(self._tray_path, uniq_id.gen32())

    @staticmethod
    def remove_internal(in_store_path):
        try:
            os.remove(in_store_path)
            return True
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise
            return False

    def remove(self, key):
        return self.remove_internal(self._discriminant(key))

    def has(self, key):
        in_store_path = self._discriminant(key)

        return os.path.exists(in_store_path)

    def get(self, key):
        in_store_path = self._discriminant(key)

        try:
            with open(in_store_path, 'r') as f:
                return f.read()
        except IOError as e:
            if e.errno == errno.ENOENT:
                raise NotInCacheError('Cannot find item by "key" {} (probing {})'.format(key, in_store_path))
            else:
                raise

    def put(self, key, value):
        inner_path = self._discriminant(key)
        tmp_path = self._gen_tmp_path()

        with open(tmp_path, 'w') as of:
            of.write(value)

        os.rename(tmp_path, inner_path)
        return inner_path

    def put_file(self, path, codec=None):
        tmp_path = self._gen_tmp_path()

        if codec:
            compress.compress(path, tmp_path, codec=codec)
        else:
            hardlink(path, tmp_path)

        key, size = hashing.git_like_hash_with_size(tmp_path)
        inner_path = self._discriminant(key)

        os.rename(tmp_path, inner_path)
        return inner_path, key, size

    def extract_file(self, key, into, codec=None, mode=None):
        inner_path = self._discriminant(key)
        into_dir = os.path.dirname(into)

        if not os.path.exists(into_dir):
            fs.create_dirs(into_dir)

        def extract():
            in_mode = os.stat(inner_path).st_mode
            if codec:
                compress.decompress(inner_path, into, codec)
                os.chmod(into, mode)
            elif mode is None or (mode & in_mode) != mode:
                logger.debug(
                    "Blob and target files have incompatible permissions (%s, %s), will NOT hardlink %s",
                    in_mode,
                    mode,
                    into,
                )
                fs.copy_file(inner_path, into)
                os.chmod(into, mode)
            else:
                hardlink(inner_path, into)

        try:
            extract()
        except (IOError, OSError) as e:
            if e.errno == errno.ENOENT:
                raise NotInCacheError('Cannot find file {} for key {}'.format(inner_path, key))
            raise

        return inner_path

    # Only single-threaded context
    def clear_tray(self):
        for file_name in sorted(os.listdir(self._tray_path)):
            self.remove_internal(os.path.join(self._tray_path, file_name))

    # Only single-threaded context
    def gc(self, ids_to_retain):
        hashed_to_retain = set(self._hash(x) for x in ids_to_retain)
        for x in self._HEX:
            for y in self._HEX:
                for fhash in os.listdir(os.path.join(self._data_store, x, y)):
                    if fhash not in hashed_to_retain:
                        self.remove_internal(os.path.join(self._data_store, fhash[0], fhash[1], fhash))


class FileStore(object):
    def __init__(self, store_path):
        self._store_path = store_path

    def __find_path(self, uid):
        return os.path.join(self._store_path, uid[0], uid[1], uid)

    def remove_uid(self, uid):
        in_store_path = self.__find_path(uid)

        if os.path.exists(in_store_path):
            return Store.remove_internal(in_store_path)
        return False

    def get(self, uid):
        in_store_path = self.__find_path(uid)

        if not os.path.exists(in_store_path):
            raise NotInCacheError('Cannot find file {} by uid {}'.format(in_store_path, uid))

        return in_store_path

    def add_file(self, file_path):
        try:
            new_hash, new_size = hashing.git_like_hash_with_size(file_path)
        except Exception as e:
            if not os.path.exists(file_path):
                raise FileNotFound('File not found: {}'.format(file_path))

            raise e

        in_store_path = self.__find_path(new_hash)

        try:
            hardlink(file_path, in_store_path)
        except Exception:
            fs.create_dirs(os.path.dirname(in_store_path))
            hardlink(file_path, in_store_path)

        return new_hash, new_size
