import os
import sys

import exts.yjson as json
import devtools.ya.tools.analyze_make.common as common

MAIN_PID = 2
CRITICAL_PATH_PID = 0
CRITICAL_PATH_TID = 0
FAILED_NODES_PID = 1
FAILED_NODES_TID = 0


def _event_args(node):
    args = common.trace_event_args(node.name, node.tag)
    if node.exit_code is not None:
        args['exit_code'] = node.exit_code
    return args


def convert_to_chromium_trace(nodes):
    threads = {}
    has_critical = False
    has_failed = False
    for node in sorted(nodes, key=lambda n: (n.start, -n.end)):
        if node.thread_name not in threads:
            threads[node.thread_name] = len(threads)
        tid = threads[node.thread_name]
        yield {
            'ph': 'B',
            'pid': MAIN_PID,
            'tid': tid,
            'ts': node.start * 1e6,
            'name': node.tag,
            'cat': 'failed' if node.is_failed else 'critical' if node.is_critical else 'other',
            'args': _event_args(node),
        }
        yield {
            'ph': 'E',
            'pid': MAIN_PID,
            'tid': tid,
            'ts': node.end * 1e6,
        }

        # Duplicate of the critical node is moved to a separate process "Critical Path"
        if node.is_critical:
            has_critical = True
            yield {
                'ph': 'B',
                'pid': CRITICAL_PATH_PID,
                'tid': CRITICAL_PATH_TID,
                'ts': node.start * 1e6,
                'name': node.tag,
                'cat': 'critical',
                'args': _event_args(node),
            }
            yield {
                'ph': 'E',
                'pid': CRITICAL_PATH_PID,
                'tid': CRITICAL_PATH_TID,
                'ts': node.end * 1e6,
            }

        # Duplicate of the failed node is moved to a separate process "Failed Nodes"
        if node.is_failed:
            has_failed = True
            yield {
                'ph': 'B',
                'pid': FAILED_NODES_PID,
                'tid': FAILED_NODES_TID,
                'ts': node.start * 1e6,
                'name': node.tag,
                'cat': 'failed',
                'args': _event_args(node),
            }
            yield {
                'ph': 'E',
                'pid': FAILED_NODES_PID,
                'tid': FAILED_NODES_TID,
                'ts': node.end * 1e6,
            }

    for index, (thread_name, tid) in enumerate(sorted(threads.items())):
        yield {'ph': 'M', 'pid': MAIN_PID, 'tid': tid, 'name': 'thread_sort_index', 'args': {'sort_index': index}}
        yield {'ph': 'M', 'pid': MAIN_PID, 'tid': tid, 'name': 'thread_name', 'args': {'name': thread_name}}

    yield {'ph': 'M', 'pid': MAIN_PID, 'name': 'process_name', 'args': {'name': 'ya make'}}
    yield {'ph': 'M', 'pid': MAIN_PID, 'name': 'process_sort_index', 'args': {'sort_index': 2}}

    if has_critical:
        yield {'ph': 'M', 'pid': CRITICAL_PATH_PID, 'name': 'process_name', 'args': {'name': 'Critical Path'}}
        yield {'ph': 'M', 'pid': CRITICAL_PATH_PID, 'name': 'process_sort_index', 'args': {'sort_index': 0}}
        yield {
            'ph': 'M',
            'pid': CRITICAL_PATH_PID,
            'tid': CRITICAL_PATH_TID,
            'name': 'thread_name',
            'args': {'name': 'Critical Path'},
        }

    if has_failed:
        yield {'ph': 'M', 'pid': FAILED_NODES_PID, 'name': 'process_name', 'args': {'name': 'Failed Nodes'}}
        yield {'ph': 'M', 'pid': FAILED_NODES_PID, 'name': 'process_sort_index', 'args': {'sort_index': 1}}
        yield {
            'ph': 'M',
            'pid': FAILED_NODES_PID,
            'tid': FAILED_NODES_TID,
            'name': 'thread_name',
            'args': {'name': 'Failed Nodes'},
        }


def main(opts):
    display = common.get_display(sys.stdout)

    file_name, nodes = common.load_evlog(opts, display, check_for_distbuild=True)

    # YA-3025
    filtered_nodes = filter(lambda n: not n.thread_name.startswith("Dummy"), nodes)

    if nodes:
        fname = os.path.abspath(file_name + '.json')
        with open(fname, 'w') as fout:
            json.dump(list(convert_to_chromium_trace(filtered_nodes)), fout)
        display.emit_message(
            f'[[imp]]Open [[alt1]]https://ui.perfetto.dev/[[imp]] or [[alt1]]about://tracing[[imp]] (deprecated) and load [[alt1]]{fname}[[imp]] file. '
        )
