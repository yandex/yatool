import threading


class TaskCache(object):
    def __init__(self, runq, default_func=None):
        self._cache = {}
        self._lock = threading.Lock()
        self._runq = runq
        self._default_func = default_func

    def __call__(self, item, func=None, deps=None, dispatch=True):
        with self._lock:
            try:
                return self._cache[item]
            except KeyError:
                func = func or self._default_func
                if not func:
                    raise
                task = func(item)
                self._cache[item] = task
                self._runq.add(task, dispatch=dispatch, deps=deps)
                return task
