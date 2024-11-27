import collections
import heapq
import logging
import threading
import time

import queue as Queue
from exts import asyncthread


logger = logging.getLogger(__name__)
logger.setLevel('INFO')


class Action(object):
    """Basic action executed by workers"""

    worker_pool_type = 'BASIC'
    __slots__ = ['_action', '_res', '_prio']

    def __init__(self, action, res=None, prio=0):
        self._action = action
        self._res = res
        self._prio = prio

    def __call__(self, *args, **kwargs):
        return self._action(*args, **kwargs)

    def __lt__(self, other):
        return self.prio() > other.prio()

    def __str__(self):
        return "Action({})".format(self._action)

    def __repr__(self):
        return str(self)

    def res(self):
        return self._res

    def prio(self):
        return self._prio


class ResInfo(object):
    """Represents resource required to execute a task"""

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


class PrioritizedTask(object):
    def __init__(self, prio, schedule_strategy, action):
        self.prio = prio
        self.total_priority = (-prio,) + tuple(fn(action) for fn in schedule_strategy)
        self.action = action

    def __lt__(self, other):
        return self.total_priority < other.total_priority


class WorkerThreads(object):
    def __init__(self, state, worker_pools, zero, cap, evlog, schedule_strategy):
        self._all_threads = []
        self._state = state
        self._out_q = Queue.Queue()
        self._active = 0

        self._active_set = collections.defaultdict(list)
        self._active_res_usage = [zero]
        self._condition = threading.Condition(threading.Lock())
        self._evlog_writer = evlog.get_writer('worker_threads') if evlog else lambda *a, **kw: None
        self._schedule_strategy = schedule_strategy

        def exec_target(worker_pool_type):
            def take_or_wait():
                while self._state.check_cancel_state():
                    with self._condition:
                        best_actions = {}
                        for k, pt_list in self._active_set.items():
                            if pt_list and k + self._active_res_usage[0] <= cap:
                                action_type = pt_list[0].action.worker_pool_type
                                prio = pt_list[0].prio
                                if action_type not in best_actions or best_actions[action_type][0] < prio:
                                    best_actions[action_type] = (prio, k)

                        if worker_pool_type in best_actions:
                            # prefer its type
                            max_prio, best_key = best_actions.pop(worker_pool_type)
                        elif best_actions:
                            # but take other types too to avoid thread locks
                            _, (max_prio, best_key) = best_actions.popitem()
                        else:
                            max_prio = best_key = None

                        if best_key is not None:
                            self._active_res_usage[0] += best_key
                            logger.debug('Active res usage %s', self._active_res_usage)
                            action = heapq.heappop(self._active_set[best_key]).action
                            self._condition.notify()
                            logger.debug('Found job %s %s with prio %s', best_key, action, max_prio)
                            return best_key, action

                        logger.debug(
                            'Cannot find any job from %s', dict((k, len(v)) for k, v in self._active_set.items())
                        )

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

        thread_num = 0
        for worker_pool_type, threads_num in worker_pools.items():
            for _ in range(threads_num):
                exec_thr = threading.Thread(
                    target=exec_target, args=(worker_pool_type,), name="Worker-{:03d}".format(thread_num + 1)
                )
                exec_thr.start()
                self._all_threads.append(exec_thr)
                thread_num += 1

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
                for ev in timings[key]:
                    self._evlog_writer(
                        'node-detailed',
                        name="{} - {}".format(key, ev.data.get('cmd')) if ev.data.get('cmd') else key,
                        time=(ev.start, ev.stop),
                        tag=key,
                    )

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
            pt = PrioritizedTask(prio=prio, schedule_strategy=self._schedule_strategy, action=action)
            with self._condition:
                self._active += 1
                heapq.heappush(self._active_set[res], pt)
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
                        self._evlog_writer(
                            'pending-exceptions', type=str(t), value=str(v), traceback=traceback.format_exc()
                        )
                    except Exception:
                        pass
            except Queue.Empty:
                break
