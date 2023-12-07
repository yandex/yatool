from __future__ import print_function
import threading
import open_hash_map


class SizeStore(object):
    FILE_SIZE = 12 * 1003001

    def __init__(self, fname):
        self._hmap = open_hash_map.OpenHashMap(fname, self.FILE_SIZE, 'Q')
        self._size = self._hmap.sum_values()
        self._lock = threading.Lock()

    def close(self):
        self._hmap.flush()

    def __setitem__(self, key, size):
        with self._lock:
            try:
                self._size -= self._hmap[key][0]
            except KeyError:
                pass
            self._hmap[key] = (size,)
            self._size += size

    def __delitem__(self, key):
        with self._lock:
            try:
                self._size -= self._hmap[key][0]
            except KeyError:
                pass
            del self._hmap[key]

    def size(self):
        return self._size

    def flush(self):
        self._hmap.flush()


if __name__ == '__main__':
    import time
    import os

    t1 = time.time()
    ss = SizeStore(os.path.abspath('ss'))
    delta = time.time() - t1
    print(delta)
