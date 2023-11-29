import time

from devtools.ya.test.programs.test_tool.lib import runtime


class Stages(object):
    def __init__(self, prefix, stage_callback=None):
        self._prefix = prefix
        self._data = {}
        self._stage = None
        self._time = None
        self._resource = 0
        self._stage_callback = stage_callback

    def set(self, name, val):
        self._data["{}_{}".format(self._prefix, name)] = val

    def add(self, name, val):
        name = "{}_{}".format(self._prefix, name)
        self._data[name] = val + self._data.get(name, 0)

    def get_duration(self, name):
        return self._data.get(self._duration_name(name))

    def stage(self, name):
        self.flush()
        self._stage = name
        self._time = time.time()
        self._resource = runtime.get_maxrss()
        if self._stage_callback:
            self._stage_callback(name)

    def get_current_stage(self):
        return self._stage, self._time

    def _duration_name(self, name):
        return "{}_{}_(seconds)".format(self._prefix, name)

    def flush(self):
        if self._stage:
            duration_name = self._duration_name(self._stage)
            self._data[duration_name] = time.time() - self._time + self._data.get(duration_name, 0)

            mem_name = "{}_{}_maxrss_(kb)".format(self._prefix, self._stage)
            self._data[mem_name] = max(self._resource, self._data.get(mem_name, 0))

        self._stage = None

    def dump(self):
        return dict(self._data)
