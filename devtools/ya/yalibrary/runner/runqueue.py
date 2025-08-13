import time
import logging
import threading
import collections

import library.python.reservoir_sampling as reservoir_sampling

from yalibrary.runner import topo
from yalibrary import status_view

logger = logging.getLogger(__name__)


class RunQueue:
    class _Wrapper:
        __slots__ = ("_run_queue", "_task", "_deps")

        def __init__(self, run_queue, task, deps):
            self._run_queue = run_queue
            self._task = task
            self._deps = deps

        def __call__(self, *args, **kwargs):
            start_time = time.time()
            self._run_queue._listener.started(self)
            notify_dependants = True
            try:
                return self._task(self._deps)
            except Exception:
                notify_dependants = False
                raise
            finally:
                end_time = time.time()
                self._run_queue._timing[self._task] = (start_time, end_time)
                self._run_queue._listener.finished(self)
                # Report first error, avoid race in exception reporting.
                if notify_dependants:
                    self._run_queue._topo.notify_dependants(self._task)

        def __lt__(self, other):
            # For https://a.yandex-team.ru/arc_vcs/devtools/ya/yalibrary/worker_threads/__init__.py?rev=7d8373415fa6f1d334941c0f126c53fac9564e67#L144 in python3
            # Copied from https://a.yandex-team.ru/arc_vcs/contrib/tools/python/src/Lib/heapq.py?rev=c4f8f494816a77f4455bb920e42336b158368695#L138
            return True  # Default option in py2 when non-comparable types x <= y

        def timing(self):
            return self._run_queue._timing[self._task]

        def __getattr__(self, name):
            return getattr(self._task, name)

        def __str__(self):
            return str(self._task)

    def __init__(self, out, listener=None):
        self._out = out
        self._listener = listener or status_view.DummyListener()
        self._lock = threading.Lock()
        self._not_dispatched = set()

        self._topo = topo.Topo()

        self._timing = {}

    def _wrap(self, task, deps):
        return RunQueue._Wrapper(self, task, deps)

    def _when_ready(self, task, deps, inplace_execution=False):
        self._listener.ready(task)
        if inplace_execution:
            self._out(self._wrap(task, deps), inplace_execution=inplace_execution)
        else:
            self._out(self._wrap(task, deps))

    def replay(self):
        def sample(s):
            return [str(t) for t in reservoir_sampling.reservoir_sampling(s, 10)]

        # Sanity checks
        unscheduled = self._topo.get_unscheduled()
        if unscheduled:
            logger.debug("Unscheduled %d tasks found: %s", len(unscheduled), sample(unscheduled))

        uncompleted = self._topo.get_uncompleted()
        if uncompleted:
            logger.debug("Uncompleted %d tasks found: %s", len(uncompleted), sample(uncompleted))

        TaskInfo = collections.namedtuple('TaskInfo', field_names=['task', 'deps', 'timing'])
        for group in self._topo.replay():
            yield [TaskInfo(task, deps, self._timing.get(task)) for task, deps in group]

    def dispatch(self, task, *args, **kwargs):
        with self._lock:
            inplace_execution = kwargs.pop('inplace_execution', False)
            if hasattr(task, 'on_dispatch'):
                task.on_dispatch(*args, **kwargs)
            self._topo.schedule_node(task, when_ready=self._when_ready, inplace_execution=inplace_execution)
            self._not_dispatched.remove(task)

    def dispatch_all(self, *args, **kwargs):
        for task in frozenset(self._not_dispatched):
            self.dispatch(task, *args, **kwargs)

    @property
    def pending(self):
        with self._lock:
            return len(self._not_dispatched)

    def add(self, task, dispatch=True, joint=None, deps=None, inplace_execution=False):
        if deps is None:
            deps = []

        with self._lock:
            assert task not in self._topo

            self._listener.add(task)

            self._topo.add_node(task)

            if joint:
                self._topo.merge_nodes(task, joint)

            self._topo.add_deps(task, *deps)

            self._not_dispatched.add(task)

        if dispatch:
            self.dispatch(task, inplace_execution=inplace_execution)
