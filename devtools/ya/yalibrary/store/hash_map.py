import struct
import errno
import os
import six

from exts import fs


def open_file(fname, size):
    fs.create_dirs(os.path.dirname(fname))
    try:
        return open(fname, "r+b")
    except IOError as e:
        if e.errno != errno.ENOENT:
            raise

        temp = "{}.{}.tmp".format(fname, os.getpid())
        with open(temp, 'wb') as f:
            f.seek(size - 1)
            f.write(b'\0')

        try:
            os.unlink(fname)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise

        os.rename(temp, fname)
        return open(fname, "r+b")


class OpenHashMap(object):
    def __init__(self, mm, fmt):
        self._mm = mm
        self._fmt = 'Q' + fmt
        self._item_size = struct.calcsize(self._fmt)
        self._buckets = len(self._mm) // self._item_size

    @staticmethod
    def _hash(key):
        import cityhash

        return cityhash.hash64(six.ensure_binary(key))

    def _offset(self, hash):
        return hash % self._buckets * self._item_size

    def __iter__(self):
        for i in six.moves.xrange(self._buckets):
            values = self._get_from(i * self._item_size)
            if values[0] != 0:
                yield values[1:]

    def __setitem__(self, key, values):
        h = self._hash(key)
        offset = self._offset(h)
        struct.pack_into(self._fmt, self._mm, offset, h, *values)

    def _get_from(self, offset):
        return struct.unpack_from(self._fmt, self._mm, offset)

    def __getitem__(self, key):
        h0 = self._hash(key)
        offset = self._offset(h0)
        values = self._get_from(offset)
        h, values = values[0], values[1:]
        if h != h0:
            raise KeyError
        return values

    def __delitem__(self, key):
        h = self._hash(key)
        offset = self._offset(h)
        self._mm[offset : offset + self._item_size] = b'\0' * self._item_size

    def flush(self):
        self._mm.flush()
