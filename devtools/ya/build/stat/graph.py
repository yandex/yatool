# cython: profile=True

import contextlib
import enum
import io
import itertools
import logging
import os
import sys
import tempfile
import typing as tp
from collections import defaultdict
from collections.abc import Iterator, Iterable, Generator
from dataclasses import dataclass, field

from yalibrary.fetcher import http_client
from yalibrary.status_view.helpers import format_paths
from devtools.ya.build.graph_description import GraphNodeUid, GraphNode, DictGraph

logger = logging.getLogger(__name__)

LOCAL_HOST = 'local'


_WorkerId = int
_Hostname = str
_PrepareType = str
_Time = float
_OptionalTime = tp.Optional[_Time]
_TimeElapsed = _OptionalTime


# This is abstract task without linkage to machine, just as in json graph
class AbstractTask:
    __slots__ = ("uid", "meta", "status", "description", "depends_on")

    class Status(enum.StrEnum):
        OK = 'OK'
        FAILED = 'FAILED'

    def __init__(self, uid: GraphNodeUid, description: str | None, meta: GraphNode):
        self.uid = uid
        self.meta = meta
        self.status = AbstractTask.Status.OK
        self.description = description
        self.depends_on: list[GraphNodeUid] = meta.get('deps', [])

    @classmethod
    def from_node(cls, node: GraphNode) -> tp.Self:
        description = format_paths(node['inputs'], node['outputs'], node.get('kv', {}))
        return cls(node['uid'], description, node)

    @classmethod
    def from_uid(cls, uid: GraphNodeUid) -> tp.Self:
        return cls(uid, None, {})


class BaseTask:
    __slots__ = (
        'uid',
        'host',
        'end_time',
        'start_time',
        'depends_on_ref',
        'from_cache',
        'critical',
        'critical_time',
        'calculating_in_progress',
        'longest_path_dep',
    )

    def __init__(self, uid: GraphNodeUid, host: _Hostname) -> None:
        self.uid = uid
        self.host = host

        # the time resource became available
        self.end_time: _OptionalTime = None
        self.start_time: _OptionalTime = None

        # graph connection
        self.depends_on_ref: list[tp.Self] = []
        self.from_cache: bool = False

        # the graph traverse needed stuff
        self.critical: bool = False
        self.critical_time: _TimeElapsed = None
        self.calculating_in_progress: bool = False
        self.longest_path_dep: tp.Self | None = None

    def get_type(self) -> str:
        return 'UNKNOWN'

    def get_dependencies(self) -> 'list[tp.Self]':
        return self.depends_on_ref

    def get_time_elapsed(self) -> _TimeElapsed:
        if (self.start_time is None) or (self.end_time is None):
            return None
        return self.end_time - self.start_time

    def name(self) -> str:
        return self.uid

    def get_type_color(self) -> str:
        raise NotImplementedError(self.__class__.__name__)

    def __repr__(self):
        return self.name()


# Todo: use @property
class RunTask(BaseTask):
    __slots__ = (
        'abstract',
        'dynamically_resolved_cache',
        'detailed_timings',
        'failures',
        'total_time',
        'count',
    )

    def __init__(self, uid: GraphNodeUid, abstract_task: AbstractTask, host: _Hostname = LOCAL_HOST) -> None:
        super().__init__(uid, host)
        self.abstract = abstract_task
        self.dynamically_resolved_cache = False
        self.detailed_timings = None
        self.failures = None
        self.total_time = False
        self.count: int | None = None

    def as_json(self):
        return {
            'type': self.get_type(cached_mark=True),
            'elapsed': self.get_time_elapsed(),
            'start_ts': self.start_time,
            'end_ts': self.end_time,
            'text': self.abstract.description,
            'platform': self.abstract.meta.get('platform'),
            'tags': self.abstract.meta.get('tags'),
            'host': self.host,
            'uid': self.uid,
        }

    def tags(self) -> 'list[str]':
        tags = [self.uid]
        tags += self.abstract.meta.get('tags', [])
        if self.host != LOCAL_HOST:
            tags.append(self.host)
        return tags

    def name(self) -> str:
        elapsed = '[{} ms] '.format(self.get_time_elapsed()) if self.get_time_elapsed() else ''
        result = '[{}] [{}]: {} '.format(
            self.get_type(cached_mark=True), ' '.join(self.tags()), self.abstract.description
        )
        return elapsed + result

    def get_type(self, cached_mark=False) -> str:
        result = 'UNKNOWN'
        if 'kv' in self.abstract.meta and 'p' in self.abstract.meta['kv']:
            result = self.abstract.meta['kv']['p']
        if cached_mark and self.from_cache:
            result += '-CACHED'
        if self.dynamically_resolved_cache:
            result += '-DYN_UID_CACHE'

        return result

    def get_type_color(self) -> str:
        if 'kv' not in self.abstract.meta or 'pc' not in self.abstract.meta['kv']:
            return 'red'
        else:
            return self.abstract.meta['kv']['pc']

    def get_colored_name(self) -> str:
        return '[[[c:{}]]{}[[rst]]] [{}]: {}'.format(
            self.get_type_color(),
            self.get_type(cached_mark=True),
            ' '.join(self.tags()),
            self.abstract.description,
        )

    def is_test(self) -> bool:
        return self.abstract.meta.get('kv', {}).get('run_test_node', False)


class PrepareTask(BaseTask):
    __slots__ = (
        'prepare_type',
        'user_type',
        'detailed_timings',
        'failures',
        'total_time',
        'count',
        'download_time_ms',
        'size',
    )

    def __init__(self, prepare_type: _PrepareType, host: _Hostname) -> None:
        super().__init__(prepare_type, host)
        self.prepare_type = prepare_type
        self.user_type: str | None = None
        self.detailed_timings = None
        self.failures = None
        self.total_time = False
        self.count: int | None = None
        self.download_time_ms: float | None = None
        self.size: int | None = None

    def as_json(self):
        return {
            'elapsed': self.get_time_elapsed(),
            'start_ts': self.start_time,
            'end_ts': self.end_time,
            'type': self.get_type(),
            'host': self.host,
        }

    def name(self) -> str:
        elapsed = '[{} ms] '.format(self.get_time_elapsed()) if self.get_time_elapsed() else ''
        result = '[{}] {}'.format(self._get_long_name(), self.host)
        return elapsed + result

    def get_type(self) -> str:
        if self.user_type is not None:
            return 'prepare:' + self.user_type
        return self._get_long_name()

    def set_type(self, str_type: str) -> None:
        self.user_type = str_type

    def get_colored_name(self) -> str:
        return '[{}] {}'.format(self._get_long_name(), self.host)

    def get_type_color(self) -> str:
        return 'gray'

    def _get_long_name(self) -> str:
        return 'prepare:' + self.prepare_type


class CopyTask(BaseTask):
    __slots__ = (
        'consuming_uid',
        'origin_host',
        'size',
    )

    def __init__(self, dep_uid: GraphNodeUid, uid: GraphNodeUid, destination_host: _Hostname) -> None:
        super().__init__(dep_uid, destination_host)
        self.consuming_uid = uid
        self.origin_host: _Hostname | None = None
        self.size: int = 0

    def as_json(self):
        return {
            'text': self._from_depends_get_attr("name"),
            'elapsed': self.get_time_elapsed(),
            'start_ts': self.start_time,
            'end_ts': self.end_time,
            'to_host': self.host,
            'from_host': self.origin_host,
            'type': self.get_type(),
            'uid': self.uid,
        }

    def name(self) -> str:
        original = self._from_depends_get_attr("name")

        elapsed = '[{} ms] '.format(self.get_time_elapsed()) if self.get_time_elapsed() else ''
        result = "[{}] (sz:{}): copy({} -> {}) result of {}".format(
            self.get_type(), self.size, self.origin_host, self.host, original
        )
        return elapsed + result

    def _from_depends_get_attr(self, attr_name: str) -> str:
        # We are interested in a RunTask only
        run_tasks = [dep for dep in self.depends_on_ref if isinstance(dep, RunTask)]
        if run_tasks:
            assert len(run_tasks) == 1
            return getattr(run_tasks[0], attr_name)()
        return 'UNKNOWN'

    def get_type(self) -> str:
        return 'Copy'

    def get_type_color(self) -> str:
        return 'brown'

    def get_colored_name(self) -> str:
        original = (self._from_depends_get_attr("get_colored_name"),)
        return "[{}] (size: {} bytes): copy({} -> {}) result of {}".format(
            self.get_type(),
            self.size,
            self.origin_host,
            self.host,
            original,
        )


class GraphStats:
    def __init__(self, graph_json: DictGraph, log_json: dict | None = None) -> None:
        self.graph_json = graph_json
        self.log_json = log_json
        self.abstract_tasks: dict[GraphNodeUid, AbstractTask] = {}
        self.run_tasks: dict[GraphNodeUid, RunTask] = {}
        self.copy_tasks: list[CopyTask] = []
        self.prepare_tasks: dict[tuple[str, _Hostname], PrepareTask] = {}
        self.failed_uids: set[GraphNodeUid] = set()
        self.time_elapsed: _TimeElapsed = None
        self._incomplete_copy_tasks: dict[tuple[GraphNodeUid, GraphNodeUid], CopyTask] = {}
        self._critical_path_candidates: list[BaseTask] = []

        # loading abstract dependency graph
        for node in self.graph_json['graph']:
            self.abstract_tasks[node['uid']] = AbstractTask.from_node(node)

    def get_prepare_task(self, prepare_type: _PrepareType, host: _Hostname = LOCAL_HOST) -> PrepareTask:
        key = (prepare_type, host)
        if key not in self.prepare_tasks:
            self.prepare_tasks[key] = PrepareTask(prepare_type, host)
        return self.prepare_tasks[key]

    def get_run_task(self, uid: GraphNodeUid, host: _Hostname = LOCAL_HOST, create: bool = True) -> RunTask | None:
        if uid not in self.run_tasks:
            if not create:
                return None
            if uid not in self.abstract_tasks:
                self.abstract_tasks[uid] = AbstractTask.from_uid(uid)
            self.run_tasks[uid] = RunTask(uid, self.abstract_tasks[uid], host)
        return self.run_tasks[uid]

    def get_copy_task(self, dep_uid: GraphNodeUid, uid: GeneratorExit, destination_host: _Hostname) -> CopyTask:
        key = (dep_uid, uid)
        if key not in self._incomplete_copy_tasks:
            task = CopyTask(dep_uid, uid, destination_host)
            self._incomplete_copy_tasks[key] = task
            return task
        else:
            task = self._incomplete_copy_tasks.pop(key)
            self.copy_tasks.append(task)
            return task

    def add_dependency_reference_safe(self, dependant: BaseTask, dependency: BaseTask) -> None:
        if dependant is None or dependant.get_time_elapsed() is None:
            return 0
        if dependency is None or dependency.get_time_elapsed() is None:
            return 0
        dependant.depends_on_ref.append(dependency)
        return 1

    def get_total_time_elapsed(self) -> _TimeElapsed:
        return self.time_elapsed

    def set_time_elapsed(self, min_time: _OptionalTime, max_time: _OptionalTime) -> None:
        if min_time is not None and max_time is not None:
            self.time_elapsed = max_time - min_time

    def get_all_nodes(self) -> "Iterator[BaseTask]":
        return itertools.chain(self.prepare_tasks.values(), self.copy_tasks, self.run_tasks.values())

    def add_critical_path_candidates(self, nodes: 'Iterable[BaseTask]') -> None:
        self._critical_path_candidates.extend(nodes)

    def get_critical_path(self) -> 'tuple[_Time, list[BaseTask]]':
        longest_path_node: BaseTask | None = None
        for node in itertools.chain(self.run_tasks.values(), self._critical_path_candidates):
            if node.get_time_elapsed() is not None:
                tm = self._calculate_critical_time(node)
                if longest_path_node is None or longest_path_node.critical_time < tm:
                    longest_path_node = node

        if longest_path_node is None:
            return 0, []

        path: list[BaseTask] = []
        p = longest_path_node
        while p is not None:
            path.append(p)
            p.critical = True
            p = p.longest_path_dep
        path.reverse()

        return longest_path_node.critical_time, path

    def _calculate_critical_time(self, node: BaseTask) -> _Time:
        """Combination of the DFS for topological sorting and the longest path calculation"""
        if node.critical_time is not None:
            return node.critical_time
        if node.calculating_in_progress:
            raise Exception("cyclic dependency detected")
        node.calculating_in_progress = True

        for dep_node in node.depends_on_ref:
            tm = self._calculate_critical_time(dep_node)
            if node.longest_path_dep is None or node.longest_path_dep.critical_time < tm:
                node.longest_path_dep = dep_node

        # At this point, all nodes that topologically come after the 'node'
        # have already been visited and have the correct critical_time value.

        node.critical_time = node.get_time_elapsed()
        if node.longest_path_dep is not None:
            node.critical_time += node.longest_path_dep.critical_time

        node.calculating_in_progress = False
        return node.critical_time


@contextlib.contextmanager
def _get_log(log: str) -> 'Generator[io.TextIOWrapper]':
    if log and log.startswith('http://'):
        with tempfile.TemporaryDirectory() as tmpdir:
            file_name = os.path.join(tmpdir, "log.txt")
            http_client.download_file(log, file_name)
            with open(file_name) as flog:
                yield flog
    else:
        with io.StringIO(log) as flog:
            yield flog


@dataclass
class _PrepareTasksTimes:
    start_time: _Time = 0
    end_times: dict[_PrepareType, _Time] = field(default_factory=dict)


class _WorkerIdToHostBinder:
    '''
    'prepare*' events don't have a 'host' attribute, only a 'worker_id'.
    We need a 'deploy' event and one of 'deployed', 'started' or 'finished' events to bind the following attributes together:
        - 'worker_id'
        - 'uid'
        - 'host'

    To reduce memory consumption, we release intermediate mappings as soon as all attributes are bound.
    '''

    def __init__(self) -> None:
        self._worker_id_to_host: dict[_WorkerId, _Hostname] = {}
        self._uid_to_worker_id: dict[GraphNodeUid, _WorkerId] = {}
        self._uid_to_host: dict[GraphNodeUid, _Hostname] = {}
        self._worker_host_is_found: set[_Hostname] = set()

    def add_uid_and_worker_id(self, uid: GraphNodeUid, worker_id: _WorkerId) -> None:
        if worker_id in self._worker_id_to_host:
            return
        if uid in self._uid_to_host:
            self._bind(worker_id, self._uid_to_host[uid])
            return
        self._uid_to_worker_id[uid] = worker_id

    def add_uid_and_host(self, uid: GraphNodeUid, host: _Hostname) -> None:
        if host in self._worker_host_is_found:
            return
        if uid in self._uid_to_worker_id:
            self._bind(self._uid_to_worker_id[uid], host)
            return
        self._uid_to_host[uid] = host

    def get_host(self, worker_id: _WorkerId) -> _Hostname:
        return self._worker_id_to_host.get(worker_id, f'worker-{worker_id}')

    def _bind(self, worker_id: _WorkerId, host: _Hostname) -> None:
        self._worker_id_to_host[worker_id] = host
        self._worker_host_is_found.add(host)
        # Cleaning happens once per host so don't bother to create reverse indices
        for uid in [k for k, v in self._uid_to_host.items() if v == host]:
            del self._uid_to_host[uid]
        for uid in [k for k, v in self._uid_to_worker_id.items() if v == worker_id]:
            del self._uid_to_worker_id[uid]


def create_graph_with_distbuild_log(graph_json: DictGraph, distbuild_log_json: dict) -> GraphStats:
    with _get_log(distbuild_log_json['log']) as flog:
        return _create_graph_with_distbuild_log(graph_json, distbuild_log_json, flog)


def _create_graph_with_distbuild_log(
    graph_json: DictGraph, distbuild_log_json: dict, flog: io.TextIOWrapper
) -> GraphStats:
    graph = GraphStats(graph_json, log_json=distbuild_log_json)

    failed_results: dict = distbuild_log_json.get('failed_results', {})
    failed_tasks_uids: list[GraphNodeUid] = failed_results.get('results', [])
    graph.failed_uids = set(failed_tasks_uids) | set(failed_results.get('fails', {}).keys())

    prepare_tasks_times: defaultdict[_WorkerId, _PrepareTasksTimes] = defaultdict(_PrepareTasksTimes)
    worker_id_to_host_binder = _WorkerIdToHostBinder()

    for uid in failed_tasks_uids:
        abstract_ref = graph.abstract_tasks[uid]
        abstract_ref.status = AbstractTask.Status.FAILED

    min_time: _OptionalTime = None
    max_time: _OptionalTime = None
    wrong_ev_types: set[str] = set()
    for line in flog:
        fields = line.rstrip().split(' ')
        if len(fields) < 3:
            continue

        time = int(fields[0])
        ev_type = fields[1]
        uid = fields[2] = sys.intern(fields[2])

        if min_time is None or min_time > time:
            min_time = time
        if max_time is None or max_time < time:
            max_time = time

        if ev_type == 'started':
            # <TIME> started <UID> <HOST>
            host = sys.intern(fields[3])
            worker_id_to_host_binder.add_uid_and_host(uid, host)
            task = graph.get_run_task(uid, host)
            task.start_time = time
        elif ev_type == 'finished':
            # <TIME> finished <UID> <HOST> <STATUS> <SIZE>
            host = sys.intern(fields[3])
            worker_id_to_host_binder.add_uid_and_host(uid, host)
            task = graph.get_run_task(uid, host)
            task.end_time = time
        elif ev_type == 'finished_from_cache':
            # <TIME> finished_from_cache <UID> <HOST_OR_WORKER_ID> <STATUS> <SIZE>
            host_or_worker_id = sys.intern(fields[3])
            task = graph.get_run_task(uid, host_or_worker_id)
            task.from_cache = True
            task.start_time = time
            task.end_time = time
        elif ev_type == 'dep_start' or ev_type == 'dep_wait':
            # <TIME> dep_wait <UID> <DESTINATION-HOST> <DEP-UID> <DEP_COUNT>
            dest_host = sys.intern(fields[3])
            dep_uid = sys.intern(fields[4])
            task = graph.get_copy_task(dep_uid, uid, dest_host)
            task.start_time = time
        elif ev_type == 'dep_finished':
            # <TIME> dep_finished <UID> <DESTINATION-HOST> <DEP-UID> <ORIGIN-HOST> <SIZE>
            dest_host = sys.intern(fields[3])
            dep_uid = sys.intern(fields[4])
            origin_host = sys.intern(fields[5])
            size = int(fields[6]) if fields[6].isnumeric() else 0
            task = graph.get_copy_task(dep_uid, uid, dest_host)
            task.origin_host = origin_host
            task.end_time = time
            task.size = size
        elif ev_type == 'deployed':
            # <TIME> deployed <UID> <HOST>
            host = sys.intern(fields[3])
            worker_id_to_host_binder.add_uid_and_host(uid, host)
            graph.get_run_task(uid, host).start_time = time
        elif ev_type == 'deploy':
            # <TIME> deploy <UID> <WORKER-ID> <READY-TASK-COUNT>
            try:
                worker_id: _WorkerId = int(fields[3])
            except ValueError:
                logger.debug("Wrong worker id value '%s' in line '%s'", fields[3], line)
                continue
            worker_id_to_host_binder.add_uid_and_worker_id(uid, worker_id)
        elif ev_type == 'prepare_start':
            # <TIME> prepare_start <SPACE> <WORKER-ID>
            try:
                worker_id: _WorkerId = int(fields[3])
            except ValueError:
                logger.debug("Wrong worker id value '%s' in line '%s'", fields[3], line)
                continue
            prepare_tasks_times[worker_id].start_time = time
        elif ev_type == 'repository_prepared':
            # <TIME> repository_prepared <PATTERN> <WORKER-ID>
            pattern = fields[2]
            try:
                worker_id: _WorkerId = int(fields[3])
            except ValueError:
                logger.debug("Wrong worker id value '%s' in line '%s'", fields[3], line)
                continue
            resource = f'repository:{pattern}'
            prepare_tasks_times[worker_id].end_times[resource] = time
        elif ev_type == 'resources_prepared':
            # <TIME> resources_prepared <EMPTY> <WORKER-ID>
            try:
                worker_id: _WorkerId = int(fields[3])
            except ValueError:
                logger.debug("Wrong worker id value '%s' in line '%s'", fields[3], line)
                continue
            prepare_tasks_times[worker_id].end_times['resources'] = time
        elif ev_type == 'dep_extract_queue' or ev_type == 'dep_extract_start' or ev_type == 'dep_extract_finish':
            # <TIME> dep_extract_queue <DEP-UID> <DESTINATION-HOST> <ORIGIN-HOST>
            # Binding an 'extract' event to a copy task requires a heavy index so it's optimal just to skip such events
            pass
        elif ev_type not in wrong_ev_types:
            wrong_ev_types.add(ev_type)
            logger.warning("Unknown event type '%s' in the line '%s'", ev_type, line)

    graph.set_time_elapsed(min_time, max_time)

    # Create prepare tasks and get the longest one for each host
    host_longest_prepare_task: dict[_Hostname, PrepareTask] = {}
    for worker_id, times in prepare_tasks_times.items():
        host = worker_id_to_host_binder.get_host(worker_id)
        last_task = None
        for prepare_type, end_time in times.end_times.items():
            task = graph.get_prepare_task(prepare_type, host)
            task.start_time = times.start_time
            task.end_time = end_time
            if last_task is None or last_task.end_time < end_time:
                last_task = task
        host_longest_prepare_task[host] = last_task

    # Replace worker_id by host in run tasks
    for run_task in graph.run_tasks.values():
        if run_task.from_cache and run_task.host.isnumeric():
            run_task.host = worker_id_to_host_binder.get_host(int(run_task.host))

    del worker_id_to_host_binder

    #############################################
    #            Creating dependencies          #
    #############################################

    dependency_count: int = 0
    for copy_task in graph.copy_tasks:
        # copy task depends on a run task which generates the resource
        orig_run_task = graph.get_run_task(copy_task.uid, create=False)
        dependency_count += graph.add_dependency_reference_safe(copy_task, orig_run_task)
        # run task depends on copy tasks which brings the resource
        dest_run_task = graph.get_run_task(copy_task.consuming_uid, create=False)
        dependency_count += graph.add_dependency_reference_safe(dest_run_task, copy_task)
        # copy task depends on a host prepare task
        prepare_task = host_longest_prepare_task.get(copy_task.host)
        dependency_count += graph.add_dependency_reference_safe(copy_task, prepare_task)

    # add other dependencies to run tasks
    for run_task in graph.run_tasks.values():
        for dep_uid in run_task.abstract.depends_on:
            dep_run_task = graph.get_run_task(dep_uid, create=False)
            dependency_count += graph.add_dependency_reference_safe(run_task, dep_run_task)
        prepare_task = host_longest_prepare_task.get(run_task.host)
        dependency_count += graph.add_dependency_reference_safe(run_task, prepare_task)

    # In distbuild we consider the prepare tasks when calculating the critical path
    graph.add_critical_path_candidates(graph.prepare_tasks.values())

    logger.debug('Dependency count in the graph is %d.', dependency_count)

    return graph


def create_graph_with_local_log(
    graph: DictGraph | GraphStats, execution_log: dict, failed_uids: 'set[GraphNodeUid] | None' = None
) -> GraphStats:
    if not isinstance(graph, GraphStats):
        graph = GraphStats(graph)
        graph.log_json = execution_log
        graph.failed_uids = failed_uids if failed_uids else set()

    for uid, info in execution_log.items():
        task = None
        if info and info.get('prepare') is not None:
            prepare_type = "{}:{}".format(info['prepare'], uid) if info['prepare'] else uid
            prepare_task = graph.get_prepare_task(prepare_type)
            if info.get('type') is not None:
                prepare_task.set_type(info['type'])

            task = prepare_task
        elif info and (info.get('timing') or info.get('cached')):
            run_task = graph.get_run_task(uid)
            run_task.dynamically_resolved_cache = info.get('dynamically_resolved_cache', False)

            task = run_task

        if task and info:

            if info.get('timing'):
                task.start_time = int(info['timing'][0] * 1000)
                task.end_time = int(info['timing'][1] * 1000)

            if info.get('detailed_timings'):
                task.detailed_timings = info['detailed_timings']

            if info.get('total_time'):
                task.total_time = True

            if info.get('count'):
                task.count = info['count']

            if info.get('failures'):
                task.failures = info['failures']

            if info.get('cached'):
                task.from_cache = True
                task.start_time = None
                task.end_time = None

            if info.get('download_time_ms'):
                task.download_time_ms = info['download_time_ms']

            if info.get('size'):
                task.size = info['size']

    dependency_count = 0
    for run_task in graph.run_tasks.values():
        for dependency_uid in run_task.abstract.depends_on:
            dep_run_task = graph.get_run_task(dependency_uid, create=False)
            dependency_count += graph.add_dependency_reference_safe(run_task, dep_run_task)

    min_time: _OptionalTime = None
    max_time: _OptionalTime = None
    for run_task in graph.run_tasks.values():
        if run_task.start_time is not None and run_task.end_time is not None:
            if min_time is None or min_time > run_task.start_time:
                min_time = run_task.start_time
            if max_time is None or max_time < run_task.end_time:
                max_time = run_task.end_time

    graph.set_time_elapsed(min_time, max_time)

    logger.debug('Node count in the dependency graph is %d.', (len(list(graph.get_all_nodes()))))
    logger.debug('Dependency count in the graph is %d.', dependency_count)

    return graph
