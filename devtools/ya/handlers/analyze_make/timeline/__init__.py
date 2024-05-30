import sys
import os

import exts.yjson as json

import devtools.ya.tools.analyze_make.common as common
import yalibrary.display
import yalibrary.formatter
import yalibrary.evlog as evlog_lib


def convert_to_chromium_trace(nodes):
    threads = {}
    for node in nodes:
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


def get_display(stream):
    formatter = yalibrary.formatter.new_formatter(is_tty=sys.stdout.isatty())
    return yalibrary.display.Display(stream, formatter)


def main(opts):
    import app_ctx

    display = get_display(sys.stdout)

    evlog_file = opts.analyze_evlog_file or app_ctx.evlog.get_latest()
    distbuild_file = opts.analyze_distbuild_json_file

    if not (evlog_file or distbuild_file):
        display.emit_message('[[bad]]One of --evlog or --distbuild-json-from-yt is required.')
        sys.exit(1)

    filepath = distbuild_file or evlog_file
    items = None
    evlog_reader = evlog_lib.EvlogReader(filepath)
    file_name = os.path.basename(filepath)

    if distbuild_file is not None:
        items = list(common.set_zero_start(common.load_from_file(evlog_reader, 'distbuild')))
    if items is None:
        items = list(common.set_zero_start(common.load_from_file(evlog_reader, 'evlog', opts.detailed)))

    if items:
        fname = file_name + '.json'
        with open(fname, 'w') as fout:
            json.dump(list(convert_to_chromium_trace(items)), fout)
        display.emit_message(f'[[imp]]Open about://tracing in Chromium and load {fname} file.')
