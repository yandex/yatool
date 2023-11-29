from itertools import chain
import os
import time
import threading

import yalibrary.store.usage_map as usage_map

from yalibrary.chunked_queue import queue


class Series(object):
    def __init__(self):
        self._lock = threading.Lock()
        self._id = 0

    def next(self):
        with self._lock:
            self._id += 1
            return self._id


class LruQueue(object):
    def __init__(self, store_path, updater=None):
        self._usage = usage_map.UsageMap(os.path.join(store_path, 'usage'))
        self._queue = queue.Queue(os.path.join(store_path, 'queue'))
        self._updater = updater
        if self._updater:
            # Postponed updates
            self._update_queue = queue.Queue(os.path.join(store_path, 'queue'))
        self._series = Series()

    def touch(self, key, update_queue=None):
        stamp = int(time.time())
        id = self._series.next()
        self._usage.touch(key, stamp=stamp, id=id)
        self._queue.push(key + '|' + str(stamp) + '|' + str(id))
        if update_queue and self._updater:
            self._update_queue.push(key + '|' + str(stamp) + '|' + str(id))

    def __action(self, consumer, value):
        """ Wrapper for consumer to use in sieve, avoids double processing """
        try:
            key, stamp, id = value.split('|')
            stamp = int(stamp)
            id = int(id)
        except ValueError:
            # TODO: logging
            return
        last_usage, last_id = self._usage.last_usage(key)
        if last_usage is None or last_usage == stamp and last_id == id:
            ret = consumer(stamp, key)
            yield key, stamp, ret

    def __strip_action(self, line_to_retain, value):
        """ Wrapper for consumer to use in sieve, avoids double processing """
        try:
            key, stamp, id = value.split('|')
            stamp = int(stamp)
            id = int(id)
        except ValueError:
            # TODO: logging
            return
        last_usage, last_id = self._usage.last_usage(key)
        if last_usage is None or last_usage == stamp and last_id == id:
            ret = line_to_retain(stamp, key)
            if ret:
                yield value

    def sieve(self, eraser, max_chunks=None):
        if self._updater:
            return chain(self._update_queue.sieve_current_chunk(lambda value: self.__action(self._updater, value)),
                         self._queue.sieve(lambda value: self.__action(eraser, value), max_chunks))

        return self._queue.sieve(lambda value: self.__action(eraser, value), max_chunks)

    def analyze(self, analyzer):
        return self._queue.analyze(lambda value: self.__action(analyzer, value))

    def flush(self):
        if self._updater:
            # Process postponed updates
            # Eager consumption of lines before truncation
            for _ in self._update_queue.sieve_current_chunk(lambda value: self.__action(self._updater, value)):
                pass
        self._queue.flush()
        self._usage.flush()

    # Should be synchronized externally
    def strip(self, uids_filter):
        return self._queue.strip(lambda value: self.__strip_action(uids_filter, value))
