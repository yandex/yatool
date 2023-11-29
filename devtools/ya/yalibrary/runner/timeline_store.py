import collections
import time
import enum


class DetailedStages(enum.Enum):
    SETUP_NODE = "setup"
    EXECUTE_COMMAND = "exec_cmd"
    POSTPROCESSING_COMMAND = "post_cmd"
    EVALUATE_NODE_RESULTS = "node_result"
    FINALIZE_NODE = "finalize"


class DetailedTimelineStore:
    def __init__(self):
        self._timeline = collections.defaultdict(list)
        self._open_stage = None

    def add_entry(self, stage, start, stop):
        self._timeline[stage].append([start, stop])

    def start_stage(self, stage, tstamp=None):
        self.finish_stage(tstamp)

        self._timeline[stage].append([tstamp or time.time()])
        self._open_stage = stage

    def finish_stage(self, tstamp=None):
        if self._open_stage:
            self._timeline[self._open_stage][-1].append(tstamp or time.time())
            self._open_stage = None

    def dump(self):
        res = collections.defaultdict(list)
        for stage in self._timeline:
            for item in self._timeline[stage]:
                if len(item) == 2:  # Stage started and ended correctly
                    res[stage.value].append(item)
        return res
