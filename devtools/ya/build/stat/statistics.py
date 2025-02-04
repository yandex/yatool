# cython: profile=True


import exts.yjson as json
import logging
import os
import time
import traceback
import copy
import collections
import enum
import math

import exts.fs
import devtools.ya.core.report

from humanfriendly import format_size

from devtools.ya.core import profiler
from devtools.ya.core import stage_tracer
import devtools.ya.test.const as test_const
from devtools.ya.build.stat.graph import create_graph_with_distbuild_log, create_graph_with_local_log, AbstractTask
from functools import cmp_to_key


class TaskType(enum.Enum):
    TEST = 'T'
    BUILD = 'B'
    DOWNLOAD = 'D'
    SERVICE = 'S'


class ExecutionStateEnum(enum.Enum):
    TESTS_ONLY = 'tests_only'
    TESTS_WITH_OTHER = 'tests_with_other'
    BUILD_ONLY = 'build_only'
    BUILD_WITH_OTHER = 'build_with_other'
    DOWNLOAD_ONLY = 'download_only'
    DOWNLOAD_WITH_OTHER = 'download_with_other'
    SERVICE_ONLY = 'service_only'
    IDLE = 'idle'


logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("statistics")

NUMBER_OF_TASKS_ELAPSED_TIME_TO_OUTPUT = 50000

SummaryResult = collections.namedtuple('SummaryResult', 'time_by_type task_execution_msec execution_stages')


def _determine_task_type(task):
    if hasattr(task, 'abstract'):
        if task.abstract.meta.get('node-type', None) == 'test':
            return TaskType.TEST
        elif task.abstract.meta.get('node-type', None) == 'download':
            return TaskType.DOWNLOAD
        else:
            return TaskType.BUILD
    elif 'from dist cache' in task.get_type():
        return TaskType.DOWNLOAD
    else:
        return TaskType.SERVICE


def _transform_timeline(timeline):
    def determine_cur_state(state):
        if state['T'] != 0 and state['B'] == 0 and state['D'] == 0 and state['S'] == 0:
            return ExecutionStateEnum.TESTS_ONLY
        elif state['T'] != 0:
            return ExecutionStateEnum.TESTS_WITH_OTHER
        elif state['B'] != 0 and state['D'] == 0 and state['S'] == 0:
            return ExecutionStateEnum.BUILD_ONLY
        elif state['B'] != 0:
            return ExecutionStateEnum.BUILD_WITH_OTHER
        elif state['D'] != 0 and state['S'] == 0:
            return ExecutionStateEnum.DOWNLOAD_ONLY
        elif state['D'] != 0:
            return ExecutionStateEnum.DOWNLOAD_WITH_OTHER
        elif state['S'] != 0:
            return ExecutionStateEnum.SERVICE_ONLY
        return ExecutionStateEnum.IDLE

    # Stages sorted by decreasing priority
    # Each stage has intervals like
    # [begin, end]
    intervals = {
        ExecutionStateEnum.TESTS_ONLY: [],
        ExecutionStateEnum.TESTS_WITH_OTHER: [],
        ExecutionStateEnum.BUILD_ONLY: [],
        ExecutionStateEnum.BUILD_WITH_OTHER: [],
        ExecutionStateEnum.DOWNLOAD_ONLY: [],
        ExecutionStateEnum.DOWNLOAD_WITH_OTHER: [],
        ExecutionStateEnum.SERVICE_ONLY: [],
        ExecutionStateEnum.IDLE: [],
    }
    last_updated_key = None
    cur_state = {
        'T': 0,
        'B': 0,
        'D': 0,
        'S': 0,
    }
    stages = {
        ExecutionStateEnum.TESTS_ONLY: 0,
        ExecutionStateEnum.TESTS_WITH_OTHER: 0,
        ExecutionStateEnum.BUILD_ONLY: 0,
        ExecutionStateEnum.BUILD_WITH_OTHER: 0,
        ExecutionStateEnum.DOWNLOAD_ONLY: 0,
        ExecutionStateEnum.DOWNLOAD_WITH_OTHER: 0,
        ExecutionStateEnum.SERVICE_ONLY: 0,
    }

    # This is an O(N) algorithm since maximum amt of entries is 2*N, where N is number of run_tasks
    tstamps = sorted(timeline.keys())
    for tstamp in tstamps:
        for message in timeline[tstamp]:
            sign = message[0]
            key = message[1]
            operation = int(sign + '1')
            cur_state[key] += operation

        key_to_update = determine_cur_state(cur_state)
        if key_to_update != last_updated_key:
            # We are at the edge of states, so we need to finish last_updated_key and start new stage
            intervals[key_to_update].append([tstamp, tstamp])
        if last_updated_key:
            intervals[last_updated_key][-1][-1] = tstamp

        last_updated_key = key_to_update

    # Flattening list to scalars
    for key in stages:
        for interval in intervals[key]:
            stages[key] += interval[1] - interval[0]

    result = dict()
    for key in stages:
        result[key.value] = stages[key]

    return result


def _task_details(task):
    if not getattr(task, 'total_time', False):
        return '[started: {}, finished: {}]'.format(task.start_time, task.end_time)
    if not task.get_time_elapsed() or not getattr(task, 'count', None):
        return ''

    msg = '[count: {:d}, cps: {:.02f}, ave time {:.02f} msec'.format(
        task.count,
        1000.0 * task.count / task.get_time_elapsed(),
        1.0 * task.get_time_elapsed() / task.count,
    )

    if getattr(task, 'failures', None):
        msg += f", failures {task.failures:d}"

    if getattr(task, 'download_time_ms', None):
        msg += f", download time {task.download_time_ms:.02f} msec"

    msg += "]"

    return msg


def print_all_tasks(graph, filename, display):
    (max_critical_time, critical_path) = graph.get_critical_path()

    tasks = [task for task in graph.get_all_nodes() if task.get_time_elapsed() is not None]
    if tasks:
        tasks.sort(key=cmp_to_key(lambda x, y: (x.start_time - y.start_time) or (x.end_time - y.end_time)))
        initial_time = tasks[0].start_time
        if filename is not None:
            with open(filename, 'w') as output_file:
                for task in tasks:
                    output_file.write(
                        '{} {} {} {}\n'.format(
                            task.name(),
                            task.start_time - initial_time,
                            task.end_time - initial_time,
                            str(task.critical),
                        )
                    )
            display.emit_message(f'Tasks are saved to {filename}')
            display.emit_message()

    return tasks, (max_critical_time, critical_path)


def print_longest_tasks(tasks, filename, display, ymake_stats=None):
    if ymake_stats:
        for configure_task in ymake_stats.threads_time:
            tasks.append(configure_task)

    max_elapsed_len = 0
    for task in tasks:
        if task.get_time_elapsed() > 0:
            max_elapsed_len = max(max_elapsed_len, get_int_length(task.get_time_elapsed()))

    json_tasks = []

    if len(tasks) > 0:
        tasks.sort(key=lambda x: -x.get_time_elapsed())

        profiler.profile_value('statistics_longest_task', tasks[0].get_time_elapsed())

        display.emit_message('The longest {:d} tasks:'.format(min(10, len(tasks))))
        for task in tasks[:10]:
            display.emit_message(
                '[{:{tw}d} ms] {} {}\n'.format(
                    int(task.get_time_elapsed()),
                    task.get_colored_name(),
                    _task_details(task),
                    tw=max_elapsed_len,
                )
            )
        display.emit_message()

        for task in tasks:
            if len(json_tasks) < NUMBER_OF_TASKS_ELAPSED_TIME_TO_OUTPUT:
                json_tasks.append(task.as_json())

        if filename is not None:
            with open(filename, 'w') as output_file:
                for task in tasks:
                    output_file.write('{} {}\n'.format(task.name(), _task_details(task)))

            with open(filename + '.json', 'w') as output_json_file:
                json.dump(json_tasks, output_json_file, indent=4, sort_keys=True)

            display.emit_message(f'Tasks sorted by elapsed time are saved to {filename}')
            display.emit_message()

    return json_tasks[:10]


def _get_node_activity(node, cached_mark=True):
    SEP = ' | '
    if node.get_type() == 'TEST':
        return SEP.join([node.get_type(), node.abstract.meta.get('env', {}).get('TEST_NAME')])  # XXX
    elif node.get_type() == 'Copy':
        copied = _get_node_activity(node.depends_on_ref[0], False) if len(node.depends_on_ref) > 0 else 'UNKNOWN'
        return SEP.join([node.get_type(), copied])
    elif 'prepare' in node.get_type():
        return node.get_type()
    else:
        return SEP.join([node.get_type(cached_mark), str(node.abstract.description)])


def profile_critical_path(critical_path):
    to_save = [(_get_node_activity(node), node.get_time_elapsed(), node.host) for node in critical_path]
    profiler.profile_value('critical_path', json.dumps(to_save))


def print_critical_path(critical_data, graph, filename, display, ymake_stats=None):
    (max_critical_time, critical_path) = critical_data
    if len(critical_path) == 0:
        display.emit_message('Critical path is empty.')
        return []

    start_time = critical_path[0].start_time
    profile_critical_path(critical_path)

    total_elapsed = 0
    max_elapsed_len = 0
    for node in critical_path:
        max_elapsed_len = max(max_elapsed_len, get_int_length(node.get_time_elapsed()))
        total_elapsed += node.get_time_elapsed()

    nodes = []

    if graph.get_total_time_elapsed() is None or total_elapsed == 0:
        display.emit_message('Critical path is empty.')
    else:
        total_elapsed = 0
        copying_elapsed = 0
        testing_elapsed = 0
        compiling_elapsed = 0
        source_prepare_elapsed = 0
        tests_data_prepare_elapsed = 0
        display.emit_message('Critical path:')
        for node in critical_path:
            nodes.append(node.as_json())

            total_elapsed += node.get_time_elapsed()
            display.emit_message(
                '[{:{tw}d} ms] {} [started: {} ({}), finished: {} ({})]\n'.format(
                    int(node.get_time_elapsed()),
                    node.get_colored_name(),
                    node.start_time - start_time,
                    node.start_time,
                    node.end_time - start_time,
                    node.end_time,
                    tw=max_elapsed_len,
                )
            )

            node_type = node.get_type()
            node_time_elapsed = node.get_time_elapsed()
            if node_type == 'Copy':
                copying_elapsed += node_time_elapsed
            elif node_type == 'TEST':
                testing_elapsed += node_time_elapsed
            elif node_type == 'source_prepare':
                source_prepare_elapsed += node_time_elapsed
            elif node_type == 'tests_data_prepare':
                tests_data_prepare_elapsed += node_time_elapsed
            else:
                compiling_elapsed += node_time_elapsed

        if ymake_stats and ymake_stats.max_timestamp_ms and ymake_stats.min_timestamp_ms:
            configuration_time = ymake_stats.max_timestamp_ms - ymake_stats.min_timestamp_ms
        else:
            configuration_time = 0

        display.emit_message(
            'Time from start: {total} ms, time elapsed by graph {graph_time} ms, time diff {diff} ms.'.format(
                total=graph.get_total_time_elapsed() + configuration_time,
                graph_time=total_elapsed,
                diff=graph.get_total_time_elapsed() + configuration_time - total_elapsed,
            )
        )

        stats = {
            'total_time': graph.get_total_time_elapsed(),
            'graph_time': total_elapsed,
            'graph_copying_time': copying_elapsed,
            'graph_testing_time': testing_elapsed,
            'graph_compiling_time': compiling_elapsed,
            'graph_source_time': source_prepare_elapsed,
            'graph_tests_data_time': tests_data_prepare_elapsed,
        }

        for k, v in stats.items():
            profiler.profile_value('statistics_{}'.format(k), v)

        stats['nodes'] = nodes

        if filename is not None:
            with open(filename, 'w') as output_file:
                total_elapsed = 0
                for node in critical_path:
                    total_elapsed += node.get_time_elapsed()

                    output_file.write('{}\n'.format(node.name()))
                    output_file.write(
                        '\t elapsed {}, started: {}({}), finished: {}({}), total_elapsed {}, diff {}\n'.format(
                            node.get_time_elapsed(),
                            node.start_time - start_time,
                            node.start_time,
                            node.end_time - start_time,
                            node.end_time,
                            total_elapsed,
                            node.end_time - start_time - total_elapsed,
                        )
                    )

                output_file.write(
                    'Time from start: {}, time elapsed by graph {}, time diff {}\n'.format(
                        graph.get_total_time_elapsed(), total_elapsed, graph.get_total_time_elapsed() - total_elapsed
                    )
                )

            filename_json = '{}.json'.format(filename)
            with open(filename_json, 'w') as output_file:
                json.dump(nodes, output_file)

            display.emit_message(f'Critical path is saved to {filename}')
        display.emit_message()

    return nodes


def print_biggest_copy_tasks(graph, filename, display):
    longest_tasks = []

    tasks = [t for t in graph.copy_tasks if t.get_time_elapsed() is not None]
    if tasks:
        tasks.sort(key=lambda x: -int(x.size))

        display.emit_message('The {} biggest copy tasks:'.format(min(10, len(tasks))))
        for task in tasks[:10]:
            display.emit_message(
                '[{} ms] '.format(task.get_time_elapsed())
                + task.get_colored_name()
                + ' '
                + '[started: {}, finished: {}]\n'.format(task.start_time, task.end_time)
            )
            longest_tasks.append(task.as_json())

        display.emit_message()

        if filename is not None:
            with open(filename, 'w') as output_file:
                for task in tasks:
                    output_file.write(
                        '[{} ms] {} [started: {}, finished: {}]\n'.format(
                            task.get_time_elapsed(), task.name(), task.start_time, task.end_time
                        )
                    )
            display.emit_message(f'Copy tasks sorted by size are saved to {filename}')
            display.emit_message()

    return longest_tasks


def print_cache_statistics(graph, filename, display):
    all_run_tasks = graph.run_tasks.values()
    dyn_cached_tasks_count = sum(1 for x in all_run_tasks if x.dynamically_resolved_cache)
    local_cached_task_count = sum(
        1 for x in graph.prepare_tasks.values() if x.get_type() == 'prepare:get from local cache'
    )
    dist_cached_task_count = sum(
        1 for x in graph.prepare_tasks.values() if x.get_type() == 'prepare:get from dist cache'
    )
    cached_task_count = dist_cached_task_count + local_cached_task_count + dyn_cached_tasks_count
    failed_task_count = sum(1 for x in all_run_tasks if x.abstract.status == AbstractTask.Status.FAILED)
    not_cached_tasks = tuple(
        x for x in all_run_tasks if not x.from_cache and x.get_time_elapsed() and not x.dynamically_resolved_cache
    )
    not_cached_task_count = len(not_cached_tasks)
    tests_task_count = sum(1 for x in not_cached_tasks if x.is_test())
    not_tests_task_count = not_cached_task_count - tests_task_count
    executed_task_count = not_cached_task_count + cached_task_count
    all_run_tasks_count = len(all_run_tasks)

    logger.debug(
        'Run tasks %d: %d cached tasks (%d cache(s) resolved by dynamic uids), %d not cached, %d failed',
        all_run_tasks_count,
        cached_task_count,
        dyn_cached_tasks_count,
        not_cached_task_count,
        failed_task_count,
    )
    logger.debug('Not cached %d: %d tests, %d not tests', not_cached_task_count, tests_task_count, not_tests_task_count)

    def safe_perc(part, total):
        return 100.0 * part / total if total > 0 else 0

    cache_hit = safe_perc(cached_task_count, executed_task_count)
    not_executed_task_count = all_run_tasks_count - not_cached_task_count
    avoided_task_count = not_executed_task_count - cached_task_count
    cache_efficiency = safe_perc(not_executed_task_count, all_run_tasks_count)

    display.emit_message(
        'Cache efficiency ratio is {:.02f}% ({:d} of {:d}). Local: {:d} ({:.02f}%), dist: {:d} ({:.02f}%), by dynamic uids: {:d} ({:.02f}%), avoided: {:d} ({:.02f}%)'.format(
            cache_efficiency,
            not_executed_task_count,
            all_run_tasks_count,
            local_cached_task_count,
            safe_perc(local_cached_task_count, all_run_tasks_count),
            dist_cached_task_count,
            safe_perc(dist_cached_task_count, all_run_tasks_count),
            dyn_cached_tasks_count,
            safe_perc(dyn_cached_tasks_count, all_run_tasks_count),
            avoided_task_count,
            safe_perc(avoided_task_count, all_run_tasks_count),
        )
    )

    stats = {
        'cache_hit': cache_hit,
        'run_tasks': all_run_tasks_count,
        'executed_tasks': executed_task_count,
        'cached_tasks': cached_task_count,
        'dyn_cached_tasks': dyn_cached_tasks_count,
        'not_cached_tasks': not_cached_task_count,
        'tests_tasks': tests_task_count,
        'failed_tasks': failed_task_count,
        'ok_tasks': not_tests_task_count,
        'avoided_tasks': avoided_task_count,
    }

    if filename is not None:
        js_data = copy.copy(stats)
        js_data['all_run_tasks'] = all_run_tasks_count

        with open(filename, 'w') as output_file:
            json.dump(js_data, output_file, indent=4, sort_keys=True)
        display.emit_message(f'Cache hit ratio is saved to {filename}')
    display.emit_message()

    for k, v in stats.items():
        profiler.profile_value('statistics_{}'.format(k), v)

    return stats


def print_dist_cache_statistics(graph, filename, display):
    log_json = graph.log_json
    if '$(yt-store-get)' in log_json:
        prefix = 'yt'
    elif '$(bazel-store-get)' in log_json:
        prefix = 'bazel'
    else:
        return

    def get_action_stat(action):
        data_size = log_json.get('$({}-store-{}-data-size)'.format(prefix, action), {}).get('data_size', 0)
        stat = log_json.get('$({}-store-{})'.format(prefix, action), {})
        timings = stat.get('timing', (0, 0))
        real_time = stat.get('real_time', 0)
        count = stat.get('count', 0)
        duration = timings[1] - timings[0]
        speed = data_size / duration if duration > 0 else 0.0
        real_speed = data_size / real_time if real_time > 0 else 0.0
        return data_size, count, speed, real_speed

    get_data_size, get_count, get_speed, get_real_speed = get_action_stat('get')
    put_data_size, put_count, put_speed, put_real_speed = get_action_stat('put')

    cache_hit_stat = log_json.get('$({}-store-cache-hit)'.format(prefix), {})
    cache_requested = cache_hit_stat.get('requested')
    cache_found = cache_hit_stat.get('found')
    # cache hit if all (really all) nodes will be requested from dist cache
    cache_fullness = 100.0 * cache_found / cache_requested if cache_requested > 0 else 0.0

    display.emit_message(
        'Dist cache download: count={}, size={}, speed={}/s'.format(
            get_count, format_size(get_data_size, binary=True), format_size(get_real_speed, binary=True)
        )
    )

    stats = {
        'cache_fullness': cache_fullness,
        'get_data_size': get_data_size,
        'get_count': get_count,
        'get_speed': get_speed,
        'get_real_speed': get_real_speed,
        'put_data_size': put_data_size,
        'put_count': put_count,
        'put_speed': put_speed,
        'put_real_speed': put_real_speed,
    }

    if filename is not None:
        with open(filename, 'w') as output_file:
            json.dump(stats, output_file, indent=4, sort_keys=True)
        display.emit_message(f'Dist cache stats are saved to {filename}')

    display.emit_message()

    return stats


def print_distbuild_download_statistics(graph, filename, display):
    try:
        total_download_size = 0
        total_time_spent_on_download = 0
        true_time_spent_on_download = 0
        cnt = 0
        for task in graph.prepare_tasks.values():
            if task.get_type() != 'prepare:download from DistBuild':
                continue

            cnt += 1

            total_time_spent_on_download += task.get_time_elapsed() or 0
            true_time_spent_on_download += task.download_time_ms or 0
            total_download_size += task.size or 0

        if total_download_size == 0:
            return

        speed = total_download_size / total_time_spent_on_download
        true_speed = total_download_size / true_time_spent_on_download
    except Exception as e:  # just in case of memory and zerodivision errors
        logger.debug("Coudn't calculate disbuild download statistics due to: %s", e)
    else:
        display.emit_message(
            'DistBuild download: count={}, size={}, speed={}/s, true speed={}/s'.format(
                cnt,
                format_size(total_download_size, binary=True),
                format_size(speed),
                format_size(true_speed),
            )
        )

        stats = {
            'total_download_size': total_download_size,
            'total_time_spent_on_download': total_time_spent_on_download,
            'true_time_spent_on_download': true_time_spent_on_download,
            'speed': speed,
            'true_speed': true_speed,
        }

        if filename is not None:
            with open(filename, 'w') as output_file:
                json.dump(stats, output_file, indent=4, sort_keys=True)
            display.emit_message(f'DistBuild download stats are saved to {filename}')

        display.emit_message()

        return stats


def print_disk_usage(task_stats, filename, display):
    if not task_stats:
        return

    disk_usage = task_stats.get('tools_disk_usage', 0)
    unknown_disk_usage = task_stats.get('tools_unknown_disk_usage', 0)
    cache_disk_usage = task_stats.get('build_cache_disk_usage', 0)
    apparent_cache_disk_usage = task_stats.get('build_cache_apparent_disk_usage', 0)

    if unknown_disk_usage:
        display.emit_message('Disk usage for tools/sdk at least {}'.format(format_size(disk_usage, binary=True)))
    else:
        display.emit_message('Disk usage for tools/sdk {}'.format(format_size(disk_usage, binary=True)))

    cache_disk_usage_str = format_size(cache_disk_usage, binary=True)
    display.emit_message('Additional disk space consumed for build cache {}'.format(cache_disk_usage_str))

    js_data = {
        'tools_disk_usage': disk_usage,
        'tools_unknown_disk_usage': unknown_disk_usage,
        'build_cache_disk_usage': cache_disk_usage,
        'build_cache_apparent_disk_usage': apparent_cache_disk_usage,
    }

    if filename is not None:
        with open(filename, 'w') as output_file:
            json.dump(js_data, output_file, indent=4, sort_keys=True)
        display.emit_message(f'Disk usage is saved to {filename}')
    display.emit_message()

    return js_data


def get_int_length(x: float) -> int:
    return len(str(int(x)))


def print_summary_times(graph, tasks, display):
    task_by_type = {}
    time_by_type = collections.defaultdict(int)
    download_time_by_type = collections.defaultdict(float)
    count_by_type = collections.Counter()

    def setup_time(task):
        type_ = task.get_type()
        time = task.get_time_elapsed()
        download_time_ms = getattr(task, "download_time_ms", None) or 0

        time_by_type[type_] += time
        task_by_type[type_] = task
        count_by_type[type_] += 1
        download_time_by_type[type_] += download_time_ms

    def get_display_msg_for_task_type(type_):
        msg = '[{:{tw}d} ms] [[[c:{}]]{}[[rst]]] [count: {:d}, ave time {:.02f} msec'.format(
            time_by_type[type_],
            task_by_type[type_].get_type_color(),
            type_,
            count_by_type[type_],
            time_by_type[type_] / count_by_type[type_],
            tw=max_elapsed_len,
        )

        if type_ == "prepare:download from DistBuild":
            avg_download_time = download_time_by_type[type_] / count_by_type[type_]
            msg += f", ave download time {avg_download_time:.02f} msec"

        msg += "]"

        return msg

    # Introducing small lang here:
    # +<Type> means that node with type <Type> began executing
    # -<Type> means that node with type <Type> stopped executing
    # <Type> may be
    # T for tests
    # D for download nodes
    # B for other nodes (counted as build nodes)
    # So, each timestamp in timeline is a list of the above-like messages

    timeline = collections.defaultdict(list)
    execution_start_time = 0
    execution_end_time = 0
    for task in tasks:
        if task.get_time_elapsed() is not None and task.get_time_elapsed() > 0:
            setup_time(task)
            if not getattr(task, 'total_time', False):
                if execution_start_time == 0:
                    execution_start_time = task.start_time
                if execution_end_time == 0:
                    execution_end_time = task.end_time
                execution_start_time = min(execution_start_time, task.start_time)
                execution_end_time = max(execution_end_time, task.end_time)
                task_type = _determine_task_type(task).value
                timeline[task.start_time].append('+{}'.format(task_type))
                timeline[task.end_time].append('-{}'.format(task_type))

    if len(time_by_type) > 0:
        display.emit_message('Total time by type:')

        max_elapsed_len = 0
        for task_type in time_by_type:
            max_elapsed_len = max(max_elapsed_len, get_int_length(time_by_type[task_type]))

        for task_type in sorted(time_by_type, key=time_by_type.get, reverse=True):
            display.emit_message(get_display_msg_for_task_type(task_type))

    total_run_task_time = 0
    total_tests_task_time = 0
    total_failed_run_task_time = 0
    for task in graph.run_tasks.values():
        elapsed = task.get_time_elapsed()
        if elapsed is not None:
            total_run_task_time += elapsed
            if task.abstract.status == AbstractTask.Status.FAILED:
                total_failed_run_task_time += elapsed
            if 'test' == task.abstract.meta.get('node-type', None):
                total_tests_task_time += elapsed

    execution_stages = _transform_timeline(timeline)
    display.emit_message()
    display.emit_message('Total tasks times:\n')

    failed_task_time_ratio = (1.0 * total_failed_run_task_time / total_run_task_time) if total_run_task_time else 1
    display.emit_message(
        'Total failed tasks time - {:.0f} ms ({:.2f}%)'.format(total_failed_run_task_time, failed_task_time_ratio * 100)
    )
    profiler.profile_value('statistics_failed_task_time_ratio', failed_task_time_ratio)
    profiler.profile_value('statistics_failed_task_time', total_failed_run_task_time)

    tests_task_time_ratio = (1.0 * total_tests_task_time / total_run_task_time) if total_run_task_time else 1
    display.emit_message(
        'Total tests tasks time - {:.0f} ms ({:.2f}%)'.format(total_tests_task_time, tests_task_time_ratio * 100)
    )
    profiler.profile_value('statistics_tests_task_time_ratio', tests_task_time_ratio)
    profiler.profile_value('statistics_tests_task_time', total_tests_task_time)

    display.emit_message('Total run tasks time - {:.0f} ms'.format(total_run_task_time))
    profiler.profile_value('statistics_run_task_time', total_run_task_time)

    return SummaryResult(
        time_by_type=time_by_type,
        task_execution_msec=execution_end_time - execution_start_time,
        execution_stages=execution_stages,
    )


def print_stages(event_log, display):
    end_events = {}
    start_events = {}
    for log_item in event_log:
        time = int(log_item['_timestamp']) // 1000
        event_type = log_item['_typename']
        if event_type == 'NEvent.TStageStarted':
            start_events[log_item['StageName']] = time
        if event_type == 'NEvent.TStageFinished':
            end_events[log_item['StageName']] = time

    if len(end_events) > 0:
        display.emit_message('Durations of build stages:')
        for stage_name, start_time in sorted(start_events.items(), key=lambda x: x[1]):
            elapsed = end_events[stage_name] - start_time
            display.emit_message(f'{stage_name} - {elapsed} ms\n')


def print_context_stages(ctx_stages, display):
    assert ctx_stages is not None

    _replaces = {'context_creation': 'Configure time'}

    display.emit_message()

    for k, v in ctx_stages.items():
        display.emit_message('{} - {:.1f} s'.format(_replaces.get(k, k), float(v)))

    return ctx_stages


def get_detailed_timings(tasks):
    # TODO: do we need to emit message to display or Snowden's enough?
    stages = collections.defaultdict(list)
    result = collections.defaultdict(dict)

    for task in tasks:
        if hasattr(task, 'detailed_timings') and task.detailed_timings is not None:
            for st_name, timings in task.detailed_timings.items():
                for ev in timings:
                    stages[st_name].append((ev.stop - ev.start) * 1000)

    for stage in stages:
        stages[stage].sort()
        for percentile in [0.50, 0.80, 0.95, 0.99, 1.0]:
            p_name = "p{}".format(int(percentile * 100))
            index = min(int(math.ceil(percentile * len(stages[stage]))) - 1, len(stages[stage]) - 1)
            result[stage][p_name] = stages[stage][index]

    return result


def _ymake_wall_time(ymake_stats):
    Interval = collections.namedtuple('Interval', ('start', 'end'))
    wall_time = 0.0
    if ymake_stats and ymake_stats.threads_time:
        intervals = sorted(Interval(t.start_time, t.end_time) for t in ymake_stats.threads_time)
        merged = intervals[:1]
        for ti in intervals[1:]:
            last_ti = merged[-1]
            if last_ti.end >= ti.start:
                merged[-1] = Interval(min(ti.start, last_ti.start), max(ti.end, last_ti.end))
            else:
                merged.append(ti)
        wall_time = sum((ti.end - ti.start) / 1e6 for ti in merged)
    return wall_time


def print_graph_statistics(
    graph,
    directory,
    event_log,
    display,
    task_stats=None,
    ctx_stages=None,
    ymake_stats=None,
    dump_debug=None,
):
    if directory is not None:
        exts.fs.create_dirs(directory)

    def _make_file_path(path):
        return None if directory is None else os.path.join(directory, path)

    stats = {}

    stats['cache_hit'] = print_cache_statistics(graph, _make_file_path('cache-hit.json'), display)
    stats['dist_cache_stat'] = print_dist_cache_statistics(graph, _make_file_path('yt-store-stat.json'), display)
    stats['disk_usage'] = print_disk_usage(task_stats, _make_file_path('disk-usage.json'), display)
    stats['distbuild_download_stat'] = print_distbuild_download_statistics(
        graph, _make_file_path('distbuild-download-stat.json'), display
    )
    tasks, critical_data = print_all_tasks(graph, _make_file_path('task-list'), display)
    stats['critical_path'] = print_critical_path(
        critical_data, graph, _make_file_path('critical-path'), display, ymake_stats
    )
    stats['longest_tasks'] = print_longest_tasks(
        copy.copy(tasks), _make_file_path('longest-tasks'), display, ymake_stats
    )
    stats['biggest_tasks'] = print_biggest_copy_tasks(graph, _make_file_path('biggest-tasks'), display)
    stats['summary_times'], stats['task_execution_msec'], stats['execution_stages_msec'] = print_summary_times(
        graph, tasks, display
    )
    stats['substages'] = get_detailed_timings(tasks)
    print_stages(event_log, display)
    if ctx_stages:
        stats['context_stages'] = print_context_stages(ctx_stages, display)
    stats['ymake_wall_time'] = _ymake_wall_time(ymake_stats)
    stats['changes_in_fs'] = _determine_fs_changes(dump_debug)
    for name, stat in stage_tracer.get_stat('graph').items():
        stats.setdefault('gg_stages', {})[name] = stat.duration

    stats['graph_lang_usage'], stats['graph_lang'] = _get_lang_statistics(graph)
    return stats


def _determine_fs_changes(dump_debug):
    """
    Determines whether there are changes in working copy based on VCS info.

    Returns True if at least one of two conditions below is met:
    1. ARCADIA_SOURCE_LAST_CHANGE is not the same as ARCADIA_PATCH_NUMBER
       It means that we are building over non-trunk branch
    2. DIRTY field is set to 'dirty'
       It means that we are building over uncommitted changes

    Returns False otherwise.
    """
    changes_marker = False

    dump_debug_data = getattr(dump_debug, 'data', {})

    vcs_info = dump_debug_data.get('vcs_info', {})

    changes_marker |= vcs_info.get('ARCADIA_SOURCE_LAST_CHANGE', '') != vcs_info.get('ARCADIA_PATCH_NUMBER', '')
    changes_marker |= vcs_info.get('DIRTY', '') == 'dirty'

    return changes_marker


def _get_lang_statistics(graph):
    lang_stat = collections.defaultdict(int)
    graph_results = set(graph.graph_json.get("result", []))
    agnostic = False

    for node in graph.graph_json['graph']:
        if node["uid"] not in graph_results:
            continue

        lang = node.get("target_properties", {}).get("module_lang", test_const.ModuleLang.UNKNOWN)
        if lang in ["py2", "py3"]:
            lang = test_const.ModuleLang.PY

        if lang == test_const.ModuleLang.LANG_AGNOSTIC:
            agnostic = True
        else:
            lang_stat[lang] += 1

    if not lang_stat:
        if agnostic:
            main_lang = test_const.ModuleLang.LANG_AGNOSTIC
        else:
            main_lang = test_const.AggregateLang.ABSENT
    elif len(lang_stat) == 1:
        main_lang = list(lang_stat).pop()
    else:
        main_lang = test_const.AggregateLang.NUMEROUS
    return lang_stat, main_lang


def report_coverage_upload_status(graph, report_file, fail_report_file):
    succeed, failed = [], []
    for task in (t for t in graph.run_tasks.values() if 'coverage_upload_node' in t.tags() and not t.from_cache):
        # was the task successful?
        if task.abstract.status == AbstractTask.Status.OK and task.start_time and task.uid not in graph.failed_uids:
            succeed.append(task.uid)
        else:
            failed.append(task.uid)
    if report_file:
        with open(report_file, 'w') as reporter:
            reporter.write('\n'.join(succeed))
    if fail_report_file:
        with open(fail_report_file, 'w') as reporter:
            reporter.write('\n'.join(failed))


def _analyze_result(graph_f, directory, opts, task_stats=None, ctx_stages=None, ymake_stats=None):
    start_time = time.time()
    statistics_stage = stager.start('statistics')
    graph = graph_f()

    if opts.print_statistics:
        import app_ctx

        stat_display = app_ctx.display
    else:
        from yalibrary.display import DevNullDisplay

        stat_display = DevNullDisplay()
    stats = {}

    try:
        import app_ctx

        dump_debug = app_ctx.dump_debug
    except Exception:
        dump_debug = None

    try:
        stats = print_graph_statistics(
            graph,
            directory,
            dict(),
            stat_display,
            task_stats=task_stats,
            ctx_stages=ctx_stages,
            ymake_stats=ymake_stats,
            dump_debug=dump_debug,
        )
    except Exception:
        stat_display.emit_message('Unable to calculate statistics because of {}'.format(traceback.format_exc()))
        logger.exception('While calculating statistics')

    if opts.coverage_succeed_upload_uids_file or opts.coverage_failed_upload_uids_file:
        report_coverage_upload_status(
            graph, opts.coverage_succeed_upload_uids_file, opts.coverage_failed_upload_uids_file
        )
    statistics_stage.finish()
    end_time = time.time()

    statistics_overhead = end_time - start_time
    stat_display.emit_message()
    stat_display.emit_message('Statistics overhead {:d} ms'.format(int(statistics_overhead * 1000)))
    if stats:
        stats['statistics_overhead_sec'] = statistics_overhead
        import app_ctx

        app_ctx.dump_debug['stats'] = stats
        devtools.ya.core.report.telemetry.report(devtools.ya.core.report.ReportTypes.GRAPH_STATISTICS, stats)


def analyze_distbuild_result(
    result, graph, directory, opts, task_stats=None, local_result=None, ctx_stages=None, ymake_stats=None
):
    def process(graph):
        graph = create_graph_with_distbuild_log(graph, result)
        if local_result:
            graph = create_graph_with_local_log(graph, local_result)
        return graph

    _analyze_result(
        lambda: process(graph), directory, opts, task_stats=task_stats, ctx_stages=ctx_stages, ymake_stats=ymake_stats
    )


def analyze_local_result(
    result, graph, directory, opts, failed_uids=None, task_stats=None, ctx_stages=None, ymake_stats=None
):
    _analyze_result(
        lambda: create_graph_with_local_log(graph, result, failed_uids),
        directory,
        opts,
        task_stats=task_stats,
        ctx_stages=ctx_stages,
        ymake_stats=ymake_stats,
    )
