import os
import logging

import six
import six.moves.cPickle as pickle

import exts.fs

logger = logging.getLogger(__name__)


class LruStore(object):
    def __init__(self, store_path):
        self.path = os.path.join(store_path, 'lru_store')
        self.data = self.load()

    def remove_uid(self, uid):
        self.data.pop(uid, None)

    def load(self):
        try:
            if os.path.isfile(self.path):
                with open(self.path, 'rb') as f:
                    return pickle.load(f)
        except Exception as e:
            logger.warning('can not load uid cache: %s', str(e))

        return {}

    def store(self):
        exts.fs.write_file(self.path, pickle.dumps(self.data, pickle.HIGHEST_PROTOCOL))

    def has(self, key):
        return key in self.data

    def try_extract(self, key):
        res = self.data.get(key, None)

        if res:
            res = dict((x, y,) for x, y, z in res)

        return res

    def flush(self):
        self.store()

    def touch(self, key, stamp):
        if key in self.data:
            for row in self.data[key]:
                row[2] = stamp

    def put(self, key, kv, stamp):
        rows = [[subkey, value, stamp] for subkey, value in six.iteritems(kv)]

        if rows:
            self.data[key] = rows
