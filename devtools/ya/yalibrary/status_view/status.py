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
    class _Listener(object):
        def __init__(self, status):
            self._status = status

        def add(self, task):
            with self._status._lock:
                self._status._qty += 1
                self._status._progress = len(self._status._finished) * 1.0 / self._status._qty

        def ready(self, task):
            pass

        def started(self, task):
            with self._status._lock:
                self._status._active[task] = time.time()

        def finished(self, task):
            with self._status._lock:
                del self._status._active[task]
                self._status._finished.append(task)
                self._status._progress = len(self._status._finished) * 1.0 / self._status._qty

    class _BulkListener(object):
        def __init__(self, status):
            self._status = status

        def add_finished(self, tasks):
            with self._status._lock:
                self._status._finished.extend(tasks)

        def set_active(self, tasks):
            tasks = sorted(tasks, key=operator.itemgetter(1))
            with self._status._lock:
                self._status._active = tasks

        def set_count(self, count):
            self._status._qty = count

        def set_progress(self, progress):
            self._status._progress = progress

    def __init__(self):
        self._active = collections.OrderedDict()
        self._finished = []
        self._lock = threading.Lock()
        self._qty = 0
        self._progress = 0.0

    def listener(self):
        return Status._Listener(self)

    def bulk_listener(self):
        return Status._BulkListener(self)

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
