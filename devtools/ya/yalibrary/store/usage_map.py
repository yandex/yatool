from __future__ import print_function
import mmap
import time
import six
from yalibrary.store import hash_map


class UsageMap(object):
    FILE_SIZE = 12 * 1003001

    def __init__(self, fname):
        self._f = hash_map.open_file(fname, self.FILE_SIZE)
        self._mm = mmap.mmap(self._f.fileno(), 0)
        self._hmap = hash_map.OpenHashMap(self._mm, 'II')

    def close(self):
        self._hmap.flush()
        self._mm.close()
        self._f.close()

    def touch(self, key, stamp=None, id=0):
        if stamp is None:
            stamp = time.time()
        self._hmap[key] = (stamp, id,)

    def last_usage(self, key):
        try:
            return self._hmap[key]
        except KeyError:
            return None, None

    def flush(self):
        self._hmap.flush()


if __name__ == '__main__':
    import contextlib

    qty = 1000000

    with contextlib.closing(UsageMap('out')) as mp:
        t1 = time.time()

        for x in six.moves.xrange(qty):
            mp.touch(str(x))

        t2 = time.time()

        washed_away = 0
        for x in six.moves.xrange(qty):
            washed_away += 1 if mp.last_usage(str(x)) is None else 0

        t3 = time.time()

        print('washed out (percent)', 100.0 * washed_away / qty)
        print('per one touch (ms)', 1000.0 * (t2 - t1) / qty)
        print('per one last_usage (ms)', 1000.0 * (t3 - t2) / qty)
