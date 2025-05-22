import os
import sys

import exts.yjson as json
import devtools.ya.tools.analyze_make.common as common


def convert_to_chromium_trace(nodes):
    threads = {}
    for node in sorted(nodes, key=lambda n: (n.start, -n.end)):
        if node.thread_name not in threads:
            threads[node.thread_name] = len(threads)
        tid = threads[node.thread_name]
        yield {
            'ph': 'B',
            'pid': 1,
            'tid': tid,
            'ts': node.start * 1e6,
            'name': node.tag,
            'cat': 'critical' if node.is_critical else 'other',
            'args': {
                'name': node.name,
            },
        }
        yield {
            'ph': 'E',
            'pid': 1,
            'tid': tid,
            'ts': node.end * 1e6,
        }

    for index, (thread_name, tid) in enumerate(sorted(threads.items())):
        yield {'ph': 'M', 'pid': 1, 'tid': tid, 'name': 'thread_sort_index', 'args': {'sort_index': index}}
        yield {'ph': 'M', 'pid': 1, 'tid': tid, 'name': 'thread_name', 'args': {'name': thread_name}}


def main(opts):
    display = common.get_display(sys.stdout)

    file_name, nodes = common.load_evlog(opts, display, check_for_distbuild=True)

    if nodes:
        fname = os.path.abspath(file_name + '.json')
        with open(fname, 'w') as fout:
            json.dump(list(convert_to_chromium_trace(nodes)), fout)
        display.emit_message(
            f'[[imp]]Open [[alt1]]about://tracing[[imp]] in Chromium and load [[alt1]]{fname}[[imp]] file.'
        )
