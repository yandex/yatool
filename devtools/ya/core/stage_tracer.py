import collections
import contextlib
import copy
import logging
import threading
import time

from abc import ABCMeta, abstractmethod

from . import stages_profiler
from . import profiler


logger = logging.getLogger(__name__)


class StagerGroups:
    OVERALL_EXECUTION = 'overall-execution'
    MODULE_LIFECYCLE = 'module-lifecycle'


class Consumer:
    __metaclass__ = ABCMeta

    @abstractmethod
    def filter(self, event):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent) -> bool
        """Filter event based on its properties"""
        raise NotImplementedError()

    @abstractmethod
    def start(self, event):
        # type: (StageTracer._StartEvent) -> None
        """Start stage with name at the start_time"""
        raise NotImplementedError()

    @abstractmethod
    def finish(self, event):
        # type: (StageTracer._FinishEvent) -> None
        """finish stage with name started at the start_time and finished at the finish_time"""
        raise NotImplementedError()


class StagesProfilerConsumer(Consumer):
    def filter(self, event):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent) -> bool
        return event.group != StagerGroups.MODULE_LIFECYCLE

    def start(self, event):
        # type: (StageTracer._StartEvent) -> None
        stages_profiler.stage_started(event.name, event.time)

    def finish(self, event):
        # type: (StageTracer._FinishEvent) -> None
        stages_profiler.stage_finished(event.name, event.time)


class ProfilerConsumer(Consumer):
    def filter(self, event):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent) -> bool
        return event.group != StagerGroups.MODULE_LIFECYCLE

    def start(self, event):
        # type: (StageTracer._StartEvent) -> None
        profiler.profile_step_started(event.name, event.time)

    def finish(self, event):
        # type: (StageTracer._FinishEvent) -> None
        profiler.profile_step_finished(event.name, event.time)


class LoggerConsumer(Consumer):
    def filter(self, event):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent) -> bool
        return event.group != StagerGroups.MODULE_LIFECYCLE

    def start(self, event):
        # type: (StageTracer._StartEvent) -> None
        logger.debug("Start stage name={}, group={}, time={}".format(event.name, event.group, event.time))

    def finish(self, event):
        # type: (StageTracer._FinishEvent) -> None
        logger.debug("Finish stage name={}, group={}, time={}".format(event.name, event.group, event.time))


class EvLogConsumer(Consumer):
    def __init__(self, evlog):
        # type: (any) -> None
        self.__evlog = evlog
        self.__evlog_writer = evlog.get_writer("stages")

    def filter(self, event):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent) -> bool
        return True

    def start(self, event):
        # type: (StageTracer._StartEvent) -> None
        pass

    def finish(self, event):
        # type: (StageTracer._FinishEvent) -> None
        if event.start_time and not self.__evlog.closed:
            event = {
                '_typename': 'stage-finished',
                '_timestamp': event.time,
                'name': event.name,
                'time': (event.start_time, event.time),
                'tag': event.name,
            }
            self.__evlog_writer(event['_typename'], **event)


class StageTracer(object):
    DEFAULT_GROUP = "default"

    class Stat(object):
        def __init__(self, intervals=None, duration=None):
            # type: (list[tuple[float, float]] | None, float | None) -> None
            self.intervals = intervals or []  # type: list[(float, float)]
            self.duration = duration or 0.0  # type: float

        def to_dict(self):
            return copy.deepcopy(self.__dict__)

    class Stage(object):
        def __init__(self, stage_tracer, name, group):
            # type: (StageTracer, str, str) -> None
            self.__stager = stage_tracer
            self.__name = name
            self.__group = group
            self.__finished = False

        def finish(self, finish_time=None):
            # type: (float | None) -> None
            if not self.__finished:
                self.__stager.finish(self.__name, self.__group, finish_time)
                self.__finished = True

    class GroupStageTracer(object):
        def __init__(self, parent, group):
            # type: (StageTracer, str) -> None
            self.__parent = parent
            self.__group = group

        def start(self, name, start_time=None):
            # type: (str, float) -> StageTracer.Stage
            return self.__parent.start(name, self.__group, start_time)

        def finish(self, name, finish_time=None):
            # type: (str, float) -> None
            return self.__parent.finish(name, self.__group, finish_time)

        def scope(self, name):
            # type: (str) -> None
            return self.__parent.scope(name, self.__group)

    def __init__(self, consumers=None):
        # type: (list[Consumer]) -> None
        self.__events = []  # type: list[StageTracer._Event]
        self.__stat = {}  # type: dict[str, dict[str, StageTracer.Stat]]
        self.__consumers = consumers or []  # type: list[Consumer]
        self.__started = {}  # type: dict[str, float]
        self.__lock = threading.Lock()

    def add_consumer(self, consumer, send_existing_events=True):
        # type: (Consumer, bool | None) -> None
        with self.__lock:
            self.__consumers.append(consumer)
            if send_existing_events:
                for event in self.__events:
                    self._consume_event(event, [consumer])

    def start(self, name, group=DEFAULT_GROUP, start_time=None):
        # type: (str, str, float) -> StageTracer.Stage
        start_time = start_time or time.time()
        with self.__lock:
            event = self._StartEvent(name, group, start_time)
            self._add_event(event)
            self.__started[(name, group)] = start_time
            return self.Stage(self, name, group)

    def finish(self, name, group=DEFAULT_GROUP, finish_time=None):
        # type: (str, str, float) -> None
        finish_time = finish_time or time.time()
        with self.__lock:
            start_time = self.__started.pop((name, group), None)
            event = self._FinishEvent(name, group, finish_time, start_time)
            self._add_event(event)
            if start_time:
                stat = self.__stat.setdefault(group, {}).setdefault(name, StageTracer.Stat())
                stat.duration += finish_time - start_time
                stat.intervals.append((start_time, finish_time))

    @contextlib.contextmanager
    def scope(self, name, group=DEFAULT_GROUP):
        # type: (str, str) -> types.GeneratorType
        stage = self.start(name, group)
        try:
            yield None
        finally:
            stage.finish()

    def get_group_tracer(self, group):
        # type: (str) -> StageTracer.GroupStageTracer
        return self.GroupStageTracer(self, group)

    def get_group_stat(self, group):
        # type: (str) -> dict[str, StageTracer.Stat]
        return copy.deepcopy(self.__stat.get(group, {}))

    def get_all_stat(self):
        return copy.deepcopy(self.__stat)

    _StartEvent = collections.namedtuple("Event", ["name", "group", "time"])
    _FinishEvent = collections.namedtuple("Event", ["name", "group", "time", "start_time"])

    def _add_event(self, event):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent) -> None
        self.__events.append(event)
        self._consume_event(event, self.__consumers)

    @staticmethod
    def _consume_event(event, consumers):
        # type: (StageTracer._StartEvent | StageTracer._FinishEvent, list[Consumer]) -> None
        for consumer in consumers:
            if consumer.filter(event):
                if isinstance(event, StageTracer._StartEvent):
                    consumer.start(event)
                elif isinstance(event, StageTracer._FinishEvent):
                    consumer.finish(event)
                else:
                    raise ValueError("Invalid event %s", event)


stage_tracer = StageTracer(
    [
        LoggerConsumer(),
        StagesProfilerConsumer(),
        ProfilerConsumer(),
    ]
)


# useful wrappers


def get_tracer(group):
    # type: (str) -> StageTracer.GroupStageTracer
    return stage_tracer.get_group_tracer(group)


def get_stat(group):
    # type: (str) -> dict[str, StageTracer.Stat]
    return stage_tracer.get_group_stat(group)


def get_all_stat():
    # type: () -> dict[str, dict[str, StageTracer.Stat]]
    return stage_tracer.get_all_stat()
