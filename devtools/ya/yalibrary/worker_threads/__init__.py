import collections
import heapq
import logging
import threading
import time

import six.moves.queue as Queue
from six.moves import xrange

from exts import asyncthread


logger = logging.getLogger(__name__)
logger.setLevel('INFO')


class Action(object):
    __slots__ = ['_action', '_res', '_prio']

    def __init__(self, action, res=None, prio=0):
        self._action = action
        self._res = res
        self._prio = prio

    def __call__(self, *args, **kwargs):
        return self._action(*args, **kwargs)

    def __lt__(self, other):
        return self.prio() > other.prio()

    def res(self):
        return self._res

    def prio(self):
        return self._prio


class ResInfo(object):
    def __init__(self, *args, **kwargs):
        self.__d = dict(*args, **kwargs)
        self._hash = hash(tuple(sorted(self.__d.items())))

    @staticmethod
    def _iter(d1, d2):
        for k in set(d1.keys()) | set(d2.keys()):
            yield k, d1.get(k, 0), d2.get(k, 0)

    def __add__(self, other):
        return ResInfo((k, v1 + v2) for k, v1, v2 in self._iter(self.__d, other.__d))

    def __sub__(self, other):
        return ResInfo((k, v1 - v2) for k, v1, v2 in self._iter(self.__d, other.__d))

    def __le__(self, other):
        return all(v1 <= v2 for _, v1, v2 in self._iter(self.__d, other.__d))

    def __eq__(self, other):
        return all(v1 == v2 for _, v1, v2 in self._iter(self.__d, other.__d))

    def __hash__(self):
        return self._hash

    def __repr__(self):
        return str(self.__d)


class WorkerThreads(object):
    def __init__(self, state, threads, zero, cap, evlog):
        self._all_threads = []
        self._state = state
        self._out_q = Queue.Queue()
        self._active = 0

        self._active_set = collections.defaultdict(list)
        self._active_res_usage = [zero]
        self._condition = threading.Condition(threading.Lock())
        self._evlog_writer = evlog.get_writer(__name__) if evlog else lambda *a, **kw: None

        def exec_target():
            def take_or_wait():
                while self._state.check_cancel_state():
                    with self._condition:
                        max_prio = None
                        best_key = None
                        for k, v in self._active_set.items():
                            if v and k + self._active_res_usage[0] <= cap:
                                if max_prio is None or max_prio < -v[0][0]:
                                    max_prio = -v[0][0]
                                    best_key = k

                        if best_key is not None:
                            self._active_res_usage[0] += best_key
                            logger.debug('Active res usage %s', self._active_res_usage)
                            _, elem = heapq.heappop(self._active_set[best_key])
                            self._condition.notify()
                            logger.debug('Found job %s %s with prio %s', best_key, elem, max_prio)
                            return best_key, elem

                        logger.debug('Cannot find any job from %s', dict((k, len(v)) for k, v in self._active_set.items()))

                        self._state.check_cancel_state()
                        self._condition.wait()

            def execute():
                while self._state.check_cancel_state():
                    res, action = take_or_wait()
                    self.__execute_action(action, res)
                    with self._condition:
                        self._active_res_usage[0] -= res
                        logger.debug('Active res usage %s', self._active_res_usage)
                        self._condition.notify()

            self._out_q.put(asyncthread.wrap(execute))

        for i in xrange(threads):
            exec_thr = threading.Thread(target=exec_target)
            exec_thr.start()
            self._all_threads.append(exec_thr)

    def __execute_action(self, action, res, inline=False):
        logger.debug('Run %s with res %s', action, res)
        name = str(action) + ("-inline" if inline else "")
        self._evlog_writer('node-started', name=name)
        start_time = time.time()
        self._out_q.put(asyncthread.wrap(action))
        end_time = time.time()
        tag = action.short_name() if hasattr(action, 'short_name') else 'notag'
        name = str(action) + ("-inline" if inline else "")
        kwargs = dict(name=name, time=(start_time, end_time), tag=tag)
        if hasattr(action, 'stat'):
            stat = action.stat()
            if stat:
                kwargs['stat'] = action.stat()
        self._evlog_writer('node-finished', **kwargs)
        if hasattr(action, "advanced_timings"):
            timings = action.advanced_timings()
            for key in timings:
                for timing in timings[key]:
                    self._evlog_writer('node-detailed', name=key, time=tuple(timing), tag=key)

    def add(self, action, inplace_execution=False):
        res = action.res()
        prio = action.prio()

        logger.debug('Add %s with res %s and prio %s', action, res, prio)
        self._state.check_cancel_state()
        if inplace_execution:
            with self._condition:
                self._active += 1
            self.__execute_action(action, res, inline=True)
        else:
            with self._condition:
                self._active += 1
                heapq.heappush(self._active_set[res], (-prio, action,))
                if len(self._active_set[res]) == 1:
                    self._condition.notify()

    def __iter__(self):
        return self

    def next(self, timeout=0.1, ready_to_stop=None):
        with self._condition:
            if self._active == 0:
                if ready_to_stop is None or ready_to_stop():
                    raise StopIteration
        try:
            value = self._out_q.get(timeout=timeout)
            with self._condition:
                self._active -= 1
            return asyncthread.unwrap(value)
        except Queue.Empty:
            return None

    def __next__(self):
        return self.next()

    def join(self):
        assert self._state.is_stopped()
        with self._condition:
            self._condition.notify_all()
        for t in self._all_threads:
            logger.debug('will join %s', t)
            t.join()
            logger.debug('join ok %s', t)

        while True:
            try:
                value = self._out_q.get_nowait()
                try:
                    asyncthread.unwrap(value)
                except Exception as e:
                    try:
                        if getattr(e, 'tame', False):
                            continue

                        import sys
                        import traceback
                        t, v, _ = sys.exc_info()
                        self._evlog_writer('pending-exceptions', type=str(t), value=str(v), traceback=traceback.format_exc())
                    except Exception:
                        pass
            except Queue.Empty:
                break
