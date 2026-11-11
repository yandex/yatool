import time
import collections
import threading
import operator

import six

# Completion counts may be supplied independently of finished task history.
ProgressCounts = collections.namedtuple("ProgressCounts", "completed total")
ProgressSnapshot = collections.namedtuple("ProgressSnapshot", "completed total fraction")


def _progress_fraction(counts):
    # type: (ProgressCounts) -> float
    return float(counts.completed) / counts.total if counts.total else 1.0


def progress_snapshot(status, history_count=None):
    # type: (object, int | None) -> ProgressSnapshot
    """Read external counts, or retain the caller's legacy history cursor."""
    # Legacy Status providers may not implement externally supplied counts.
    read_counts = getattr(status, "progress_counts", None)
    counts = read_counts() if read_counts is not None else None
    if counts is not None:
        return ProgressSnapshot(counts.completed, counts.total, _progress_fraction(counts))
    completed = status.done() if history_count is None else history_count
    total = status.count
    return ProgressSnapshot(completed, total, float(completed) / max(1, total))


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

        def set_progress_counts(self, counts):
            # type: (ProgressCounts) -> None
            # Update authoritative work counts independently of task history.
            with self._status._lock:
                self._status._progress_counts = counts
                self._status._qty = counts.total
                self._status._progress = _progress_fraction(counts)

    def __init__(self):
        self._active = collections.OrderedDict()
        self._auxiliary = collections.OrderedDict()
        self._finished = []
        self._lock = threading.Lock()
        self._qty = 0
        self._progress = 0.0
        self._progress_counts = None

    def listener(self):
        return Status._Listener(self)

    def bulk_listener(self):
        return Status._BulkListener(self)

    def finished(self, since):
        with self._lock:
            return self._finished[since:]

    def progress(self):
        return self._progress

    def progress_counts(self):
        # type: () -> ProgressCounts | None
        with self._lock:
            return self._progress_counts

    def done(self):
        with self._lock:
            return len(self._finished)

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

    def stop_activity(self, task):
        # type: (object) -> None
        # End activity independently of the completed-command output history.
        with self._lock:
            del self._active[task]

    def start_auxiliary(self, task):
        with self._lock:
            self._auxiliary[task] = time.time()

    def finish_auxiliary(self, task):
        with self._lock:
            self._auxiliary.pop(task, None)

    def auxiliary_active(self):
        with self._lock:
            now = time.time()
            return [(task, now - started) for task, started in self._auxiliary.items()]

    def display_active(self):
        return sorted(self.active() + self.auxiliary_active(), key=lambda item: -item[1])
