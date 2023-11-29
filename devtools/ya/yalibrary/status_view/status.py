import time
import collections
import threading
import operator

import six


class DummyListener(object):
    @staticmethod
    def add(*args, **kwargs):
        pass

    @staticmethod
    def ready(*args, **kwargs):
        pass

    @staticmethod
    def started(*args, **kwargs):
        pass

    @staticmethod
    def finished(*args, **kwargs):
        pass


class Status(object):
    def __init__(self):
        self._active = collections.OrderedDict()
        self._finished = []
        self._lock = threading.Lock()
        self._qty = 0
        self._progress = 0.0

    def listener(self):
        class Listener(object):
            @staticmethod
            def add(task):
                with self._lock:
                    self._qty += 1
                    self._progress = len(self._finished) * 1.0 / self._qty

            def ready(self, task):
                pass

            @staticmethod
            def started(task):
                with self._lock:
                    self._active[task] = time.time()

            @staticmethod
            def finished(task):
                with self._lock:
                    del self._active[task]
                    self._finished.append(task)
                    self._progress = len(self._finished) * 1.0 / self._qty

        return Listener()

    def bulk_listener(self):
        class BulkListener(object):
            @staticmethod
            def add_finished(tasks):
                with self._lock:
                    self._finished.extend(tasks)

            @staticmethod
            def set_active(tasks):
                tasks = tasks[:]
                tasks.sort(key=operator.itemgetter(1))

                with self._lock:
                    self._active = tasks

            @staticmethod
            def set_count(count):
                self._qty = count

            @staticmethod
            def set_progress(progress):
                self._progress = progress

        return BulkListener()

    def finished(self, since):
        with self._lock:
            return self._finished[since:]

    def progress(self):
        return self._progress

    @property
    def count(self):
        return self._qty

    def active(self):
        with self._lock:
            now = time.time()
            try:
                iter_over = six.iteritems(self._active)
            except AttributeError:
                iter_over = self._active
            return [(task, now - tm) for task, tm in iter_over]
