# cython: profile=True

import contextlib
import io
import logging
import os
import sys
import tempfile
from collections import defaultdict

from exts import http_client
from yalibrary.status_view.helpers import format_paths

logger = logging.getLogger(__name__)

LOCAL_HOST = 'local'
# Current UNIX time is somewhere around 2^31,
# it is reasonable to use 2^64 as sort of infinite value.
# in case of undefined start time
MAX_START_TIME = 2**64
MIN_END_TIME = 0


# This is abstract task without linkage to machine, just as in json graph
class AbstractTask:
    __slots__ = ("uid", "meta", "status", "description", "depends_on")

    def __init__(self, uid, description, meta):
        self.uid = uid
        self.meta = meta
        self.status = 'OK'
        self.description = description
        self.depends_on = []


# This abstract class represents two conceptions:
# 1) Resource available on some machine. This resource can be created there or copied from another machine
# 2) Node in critical path algorithm.
class ResourceNode:
    __slots__ = (
        'uid',
        'host',
        'end_time',
        'start_time',
        'depends_on_ref',
        'visited',
        'critical',
        'critical_time',
        'calculating_in_progress',
        'from_cache',
    )

    def __init__(self, uid, host):
        self.uid = uid
        self.host = host

        # the time resource became available
        self.end_time = None  # type: int | float | None
        self.start_time = None  # type: int | float | None

        # graph connection
        self.depends_on_ref = []

        # the graph traverse needed shit
        self.visited = False
        self.critical = False
        self.critical_time = 0
        self.calculating_in_progress = False

        self.from_cache = False

    def is_fake(self):
        return False

    def get_key(self):
        return (self.uid, self.host)

    def get_type(self):
        return 'UNKNOWN'

    def get_dependencies(self):
        return self.depends_on_ref

    def get_time_elapsed(self):
        if (self.start_time is None) or (self.end_time is None):
            return None
        return self.end_time - self.start_time

    def name(self):
        return self.uid

    def get_type_color(self):
        raise NotImplementedError(self.__class__.__name__)


class FakeResourceNode(ResourceNode):
    __slots__ = ()

    def __init__(self):
        super().__init__("(root)", "(fake_host)")

    def is_fake(self):
        return True


# Todo: use @property
class RunTask(ResourceNode):
    __slots__ = (
        'abstract',
        'deploy_start_time',
        'deployed_time',
        'deploy_task',
        'dynamically_resolved_cache',
        'detailed_timings',
        'failures',
        'total_time',
        'count',
    )

    def __init__(self, uid, host):
        super().__init__(uid, host)
        self.abstract = None
        self.deploy_start_time = None
        self.deployed_time = None
        self.deploy_task = None
        self.dynamically_resolved_cache = False
        self.detailed_timings = None
        self.failures = None
        self.total_time = False
        self.count = None

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

    def tags(self):
        tags = [self.uid]
        tags += self.abstract.meta.get('tags', [])
        if self.host != LOCAL_HOST:
            tags.append(self.host)
        return tags

    def name(self):
        elapsed = '[{} ms] '.format(self.get_time_elapsed()) if self.get_time_elapsed() else ''
        result = '[{}] [{}]: {} '.format(
            self.get_type(cached_mark=True), ' '.join(self.tags()), self.abstract.description
        )
        return elapsed + result

    def get_type(self, cached_mark=False):
        result = 'UNKNOWN'
        if 'kv' in self.abstract.meta and 'p' in self.abstract.meta['kv']:
            result = self.abstract.meta['kv']['p']
        if cached_mark and self.from_cache:
            result += '-CACHED'
        if self.dynamically_resolved_cache:
            result += '-DYN_UID_CACHE'

        return result

    def get_type_color(self):
        if 'kv' not in self.abstract.meta or 'pc' not in self.abstract.meta['kv']:
            return 'red'
        else:
            return self.abstract.meta['kv']['pc']

    def get_colored_name(self):
        return '[[[c:{}]]{}[[rst]]] [{}]: {}'.format(
            self.get_type_color(),
            self.get_type(cached_mark=True),
            ' '.join(self.tags()),
            self.abstract.description,
        )

    def is_test(self):
        return self.abstract.meta.get('kv', {}).get('run_test_node', False)


class PrepareTask(ResourceNode):
    __slots__ = (
        'prepare_type',
        'user_type',
        'detailed_timings',
        'failures',
        'total_time',
        'count',
    )

    def __init__(self, prepare_type, host):
        super().__init__(prepare_type, host)
        self.prepare_type = prepare_type
        self.user_type = None
        self.detailed_timings = None
        self.failures = None
        self.total_time = False
        self.count = None

    def as_json(self):
        return {
            'elapsed': self.get_time_elapsed(),
            'start_ts': self.start_time,
            'end_ts': self.end_time,
            'type': self.get_type(),
            'host': self.host,
        }

    def name(self):
        elapsed = '[{} ms] '.format(self.get_time_elapsed()) if self.get_time_elapsed() else ''
        result = '[{}] {}'.format(self._get_long_name(), self.host)
        return elapsed + result

    def get_type(self):
        if self.user_type is not None:
            return 'prepare:' + self.user_type
        return self._get_long_name()

    def set_type(self, str_type):
        self.user_type = str_type

    def get_colored_name(self):
        return '[%s] %s' % (self._get_long_name(), self.host)

    def get_type_color(self):
        return 'gray'

    def _get_long_name(self):
        return 'prepare:' + self.prepare_type


class CopyTask(ResourceNode):
    __slots__ = (
        'resource',
        'origin_host',
        'destination_host',
        'wait',
        'can_start_after',
        'size',
        'stages',
    )

    def __init__(self, resource, origin_host, destination_host, wait):
        super().__init__(resource, destination_host)
        self.resource = resource
        self.origin_host = origin_host
        self.destination_host = destination_host
        self.wait = wait
        self.can_start_after = None
        self.size = 0
        self.stages = {}

    def as_json(self):
        return {
            'text': self._from_depends_get_attr("name"),
            'elapsed': self.get_time_elapsed(),
            'start_ts': self.start_time,
            'end_ts': self.end_time,
            'to_host': self.destination_host,
            'from_host': self.origin_host,
            'type': self.get_type(),
            'uid': self.uid,
        }

    def setup_copy_stage(self, operation, time):
        self.stages[operation] = time

    def name(self):
        original = self._from_depends_get_attr("name")

        elapsed = '[{} ms] '.format(self.get_time_elapsed()) if self.get_time_elapsed() else ''
        result = "[{}] (sz:{}): copy({} -> {}) result of {}".format(
            self.get_type(), self.size, self.origin_host, self.destination_host, original
        )
        return elapsed + result

    def _from_depends_get_attr(self, attr_name):
        if len(self.depends_on_ref) > 0:
            node = self.depends_on_ref[0]
            if node is self:
                original = "SELF (CYCLIC DEPENDENCY)"
                logger.warning("Cyclic dependency detected for node %s depends on %s", self.uid, node.uid)
            else:
                original = getattr(node, attr_name)()
        else:
            original = 'UNKNOWN'
        return original

    def get_type(self):
        return 'Copy'

    def get_type_color(self):
        return 'brown'

    def get_colored_name(self):
        original = (self._from_depends_get_attr("get_colored_name"),)
        return "[{}] (size: {} bytes): copy({} -> {}) result of {}".format(
            'Copy',
            self.size,
            self.origin_host,
            self.destination_host,
            original,
        )


class Graph:
    def __init__(self, graph_json):
        self.graph_json = graph_json
        self.log_json = None
        self.abstract_tasks = {}
        self.run_tasks = {}
        self.copy_tasks = {}
        self.prepare_tasks = {}
        self.resource_nodes = defaultdict(list)
        self.failed_uids = set()

        # loading abstract dependency graph
        graph = self.graph_json['graph']
        for node_info in graph:
            description = format_paths(node_info['inputs'], node_info['outputs'], node_info.get('kv', []))
            task = self.get_abstract_task(node_info['uid'], description, node_info)
            task.depends_on = node_info['deps']

        # adding fake root for convenience
        self.fake_resource_node = FakeResourceNode()

    def get_resource_node_list(self, node):
        return self.resource_nodes[node.get_key()]

    def get_run_or_copy_task(self, uid, host):
        task = RunTask(uid, host)
        if task.get_key() in self.run_tasks:
            return self.run_tasks[task.get_key()]

        task = CopyTask(uid, None, host, None)
        if task.get_key() in self.copy_tasks:
            return self.copy_tasks[task.get_key()]

        return self.get_run_task(uid, host)

    def get_abstract_task(self, uid, description, meta):
        if uid not in self.abstract_tasks:
            self.abstract_tasks[uid] = AbstractTask(uid, description, meta)
        return self.abstract_tasks[uid]

    def get_prepare_task(self, prepare_type, host):
        task = PrepareTask(prepare_type, host)
        if task.get_key() not in self.prepare_tasks:
            self.get_resource_node_list(task).append(task)
            self.prepare_tasks[task.get_key()] = task

        return self.prepare_tasks[task.get_key()]

    def get_host_prepare_tasks(self, host):
        return [task for task in self.prepare_tasks.values() if task.host == host]

    def get_run_task(self, uid, host):
        task = RunTask(uid, host)
        if task.get_key() not in self.run_tasks:
            task.abstract = self.get_abstract_task(uid, None, {})
            self.get_resource_node_list(task).append(task)
            self.run_tasks[task.get_key()] = task

        return self.run_tasks[task.get_key()]

    def get_copy_task(self, uid, origin_host, destination_host, wait):
        task = CopyTask(uid, origin_host, destination_host, wait)
        if task.get_key() not in self.copy_tasks:
            self.get_resource_node_list(task).append(task)
            self.copy_tasks[task.get_key()] = task

        return self.copy_tasks[task.get_key()]

    def add_dependency_reference(self, dependant, dependency):
        if dependant is None:
            raise Exception("adding empty dependency: dependant is None")

        if dependency is None:
            raise Exception("adding empty dependency: dependency is None")

        dependant.depends_on_ref.append(dependency)

    def get_total_time_elapsed(self):
        return self.fake_resource_node.get_time_elapsed()


@contextlib.contextmanager
def _get_log(log):
    if log and log.startswith('http://'):
        with tempfile.TemporaryDirectory() as tmpdir:
            file_name = os.path.join(tmpdir, "log.txt")
            http_client.download_file(log, file_name)
            with open(file_name) as flog:
                yield flog
    else:
        with io.StringIO(log) as flog:
            yield flog


def create_graph_with_distbuild_log(graph_json, distbuild_log_json):
    with _get_log(distbuild_log_json['log']) as flog:
        return _create_graph_with_distbuild_log(graph_json, distbuild_log_json, flog)


def _create_graph_with_distbuild_log(graph_json, distbuild_log_json, flog):
    graph = Graph(graph_json)
    graph.log_json = distbuild_log_json

    failed_results = distbuild_log_json.get('failed_results', {})
    failed_tasks_uids = failed_results.get('results', [])
    graph.failed_uids = set(failed_tasks_uids) | set(failed_results.get('fails', {}).keys())

    prepare_finish_ev_types = [
        'toolchain_prepare_finish',
        'resources_prepare_finish',
        'repository_prepared',
        'source_prepare_finish',
        'tests_data_prepare_finish',
        'resources_prepared',
    ]

    def get_prepare_type(ev_type):
        return '_'.join(ev_type.split('_')[:-1])

    prepare_types = list(map(get_prepare_type, prepare_finish_ev_types))

    start_time = 0
    for i in flog:
        i.rstrip()
        lines = i.split(' ')
        if len(lines) < 3:
            continue

        time = int(lines[0])
        ev_type = lines[1]
        uid = sys.intern(lines[2])
        host = "(n/a)"

        if len(lines) > 3:
            host = sys.intern(lines[3])

        start_time = min(start_time, time)
        abstract_ref = graph.abstract_tasks.get(uid)
        if uid in failed_tasks_uids:
            abstract_ref.status = 'FAILED'

        if ev_type == 'started':
            graph.get_run_task(uid, host).start_time = time
        elif ev_type == 'finished':
            graph.get_run_task(uid, host).end_time = time
        elif ev_type == 'dep_start' or ev_type == 'dep_wait':
            # <TIME> dep_wait  <UID> <DESTINATION-HOST> <DEP-UID> <DEP_COUNT>
            dep_uid = sys.intern(lines[4])

            graph.get_copy_task(dep_uid, None, host, ev_type == 'dep_wait').start_time = time
        elif ev_type == 'dep_finished':
            # <TIME> dep_finished <UID> <DESTINATION-HOST> <DEP-UID> <ORIGIN-HOST> <SIZE>
            dep_uid = sys.intern(lines[4])
            original_host = sys.intern(lines[5])
            try:
                size = int(lines[6])
            except ValueError:
                size = 0

            task = graph.get_copy_task(dep_uid, original_host, host, False)
            task.origin_host = original_host
            task.end_time = time
            task.size = size
        elif ev_type == 'finished_from_cache':
            task = graph.get_run_or_copy_task(uid, host)
            task.from_cache = True
            task.start_time = time
            task.end_time = time
        elif ev_type == 'deployed':
            graph.get_run_task(uid, host).start_time = time
        elif ev_type == 'deploy' or ev_type == 'deploy_identic':
            pass
        elif ev_type == 'ready':
            pass
        elif (
            ev_type == 'dep_pack_start'
            or ev_type == 'dep_pack_finish'
            or ev_type == 'dep_send_start'
            or ev_type == 'dep_send_finish'
        ):
            # <TIME> dep_pack_start <DEP-UID> <DESTINATION-HOST> <ORIGIN-HOST>
            original_host = sys.intern(lines[4])
            task = graph.get_copy_task(uid, original_host, host, False)
            task.setup_copy_stage(ev_type, time)
        elif ev_type == 'dep_extract_queue' or ev_type == 'dep_extract_start' or ev_type == 'dep_extract_finish':
            # <TIME> dep_extract_queue <DEP-UID> <DESTINATION-HOST> <ORIGIN-HOST>
            original_host = sys.intern(lines[4])
            task = graph.get_copy_task(uid, original_host, host, False)
            task.setup_copy_stage(ev_type, time)
        elif ev_type == 'prepare_start':
            # here host goes third, not uid
            host = uid
            for prepare_type in prepare_types:
                graph.get_prepare_task(prepare_type, host).start_time = time
        elif ev_type in prepare_finish_ev_types:
            # here host goes third again
            host = uid
            prepare_type = get_prepare_type(ev_type)
            graph.get_prepare_task(prepare_type, host).end_time = time
        else:
            raise Exception("unknown event type \'{}\' in the line {}".format(ev_type, lines))

    #############################################
    #            Creating dependencies          #
    #############################################
    # we have all nodes of the graph, we should tie them with dependencies
    # first of all sort all nodes, to determine first available resource on server

    for run_task in graph.run_tasks.values():
        graph.add_dependency_reference(graph.fake_resource_node, run_task)

        task_start_time = run_task.start_time or MAX_START_TIME
        task_end_time = run_task.end_time or MIN_END_TIME
        if graph.fake_resource_node.start_time is None:
            graph.fake_resource_node.start_time = task_start_time

        if graph.fake_resource_node.end_time is None:
            graph.fake_resource_node.end_time = task_end_time

        graph.fake_resource_node.start_time = min(graph.fake_resource_node.start_time, task_start_time)
        graph.fake_resource_node.end_time = max(graph.fake_resource_node.end_time, task_end_time)

    for nodes in graph.resource_nodes.values():
        nodes.sort(key=lambda x: x.end_time or (start_time + 1000000000))

    dependency_count = 0
    for run_task in graph.run_tasks.values():
        for dependency_uid in run_task.abstract.depends_on:
            resource_nodes = graph.get_resource_node_list(ResourceNode(dependency_uid, run_task.host))
            if len(resource_nodes) != 0:
                graph.add_dependency_reference(run_task, resource_nodes[0])
                dependency_count += 1

    for copy_task in graph.copy_tasks.values():
        if copy_task.origin_host != copy_task.destination_host:
            resource_nodes = graph.get_resource_node_list(ResourceNode(copy_task.resource, copy_task.origin_host))
            if len(resource_nodes) != 0:
                graph.add_dependency_reference(copy_task, resource_nodes[0])
                dependency_count += 1

    for run_task in graph.run_tasks.values():
        prepare_tasks = graph.get_host_prepare_tasks(run_task.host)
        for dependency in prepare_tasks:
            resource_nodes = graph.get_resource_node_list(ResourceNode(dependency.uid, run_task.host))
            if len(resource_nodes) != 0:
                graph.add_dependency_reference(run_task, resource_nodes[0])
                dependency_count += 1

    for copy_task in graph.copy_tasks.values():
        prepare_tasks = graph.get_host_prepare_tasks(copy_task.destination_host)
        for dependency in prepare_tasks:
            resource_nodes = graph.get_resource_node_list(ResourceNode(dependency.uid, copy_task.destination_host))
            if len(resource_nodes) != 0:
                graph.add_dependency_reference(copy_task, resource_nodes[0])
                dependency_count += 1

    for prepare_task in graph.prepare_tasks.values():
        graph.add_dependency_reference(graph.fake_resource_node, prepare_task)

    logger.debug('Node count in the dependency graph is %d.' % (len(graph.resource_nodes)))
    logger.debug('Dependency count in the graph is %d.' % dependency_count)

    return graph


def create_graph_with_local_log(graph, execution_log, failed_uids=None):
    if not isinstance(graph, Graph):
        graph = Graph(graph)
        graph.log_json = execution_log
        graph.failed_uids = failed_uids if failed_uids else set()

    for uid, info in execution_log.items():
        task = None
        if info and info.get('prepare') is not None:
            prepare_type = "{}:{}".format(info['prepare'], uid) if info['prepare'] else uid
            prepare_task = graph.get_prepare_task(prepare_type, LOCAL_HOST)
            if info.get('type') is not None:
                prepare_task.set_type(info['type'])

            task = prepare_task
        elif info and (info.get('timing') or info.get('cached')):
            run_task = graph.get_run_task(uid, LOCAL_HOST)
            run_task.dynamically_resolved_cache = info.get('dynamically_resolved_cache', False)

            task = run_task

        if task and info and info.get('timing'):
            task.start_time = int(info['timing'][0] * 1000)
            task.end_time = int(info['timing'][1] * 1000)

        if task and info and info.get('detailed_timings'):
            task.detailed_timings = info['detailed_timings']

        if task and info and info.get('total_time'):
            task.total_time = True

        if task and info and info.get('count'):
            task.count = info['count']

        if task and info and info.get('failures'):
            task.failures = info['failures']

        if task and info and info.get('cached'):
            task.from_cache = True
            task.start_time = None
            task.end_time = None

    dependency_count = 0
    for run_task in graph.run_tasks.values():
        for dependency_uid in run_task.abstract.depends_on:
            resource_nodes = graph.get_resource_node_list(ResourceNode(dependency_uid, run_task.host))
            if len(resource_nodes) != 0:
                graph.add_dependency_reference(run_task, resource_nodes[0])
                dependency_count += 1

    for run_task in graph.run_tasks.values():
        if run_task.start_time is not None and run_task.end_time is not None:
            graph.add_dependency_reference(graph.fake_resource_node, run_task)
            if graph.fake_resource_node.end_time is None:
                graph.fake_resource_node.start_time = run_task.start_time
                graph.fake_resource_node.end_time = run_task.end_time

            graph.fake_resource_node.start_time = min(graph.fake_resource_node.start_time, run_task.start_time)
            graph.fake_resource_node.end_time = max(graph.fake_resource_node.end_time, run_task.end_time)

    logger.debug('Node count in the dependency graph is %d.' % (len(graph.resource_nodes)))
    logger.debug('Dependency count in the graph is %d.' % dependency_count)

    return graph


def reset_critical_path(node):
    if node.critical_time == 0:
        return

    node.visited = False
    node.critical = False
    node.critical_time = 0
    node.calculating_in_progress = False
    for neighbouring_node in node.get_dependencies():
        reset_critical_path(neighbouring_node)


def calculate_critical_time(node):
    if node.calculating_in_progress:
        raise Exception("cyclic dependency detected")

    if node.visited:
        return node.critical_time

    if node.get_time_elapsed() is None:
        return 0

    node.visited = True
    node.calculating_in_progress = True
    node.critical_time = node.get_time_elapsed()
    for neighbouring_node in node.get_dependencies():
        current_critical_time = node.get_time_elapsed()
        if neighbouring_node.end_time is None or node.start_time >= neighbouring_node.end_time or node.is_fake():
            current_critical_time += calculate_critical_time(neighbouring_node)
        if current_critical_time > node.critical_time:
            node.critical_time = current_critical_time

    node.calculating_in_progress = False
    return node.critical_time


def restore_critical_path(node, path):
    critical_time = calculate_critical_time(node)
    if critical_time == 0:
        return

    if critical_time == node.get_time_elapsed() or node.get_time_elapsed() is None:
        path.append(node)
        return

    for neighbouring_node in node.get_dependencies():
        current_critical_time = node.get_time_elapsed()
        if neighbouring_node.end_time is None or node.start_time >= neighbouring_node.end_time or node.is_fake():
            current_critical_time += calculate_critical_time(neighbouring_node)
        if current_critical_time > node.critical_time:
            raise Exception("critical time is incorrect")

        if current_critical_time == node.critical_time:
            restore_critical_path(neighbouring_node, path)
            path.append(node)
            return

    raise Exception("cannot find next node in the critical path")


def get_critical_path(graph):
    reset_critical_path(graph.fake_resource_node)
    critical_path = []
    max_critical_time = calculate_critical_time(graph.fake_resource_node)
    restore_critical_path(graph.fake_resource_node, critical_path)
    return max_critical_time, [p for p in critical_path if not p.is_fake()]
