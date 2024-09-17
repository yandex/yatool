import collections
import json
import logging
import os

from yatest.common import cores
from devtools.ya.test.util import shared

logger = logging.getLogger(__name__)

Entry = collections.namedtuple('Entry', ['cmd', 'pid', 'binary_path', 'cwd'])


def parse_core_lines(data, func, limit):
    limit = limit or -1

    def parse(line):
        try:
            d = json.loads(line)
            return Entry(d.get('cmd'), d.get('pid'), d.get('binary_path'), d.get('cwd'))
        except ValueError as e:
            logger.exception("Failed to parse line '%s': %s", line, e)
            return Entry(None, None, None, None)

    dropped = set()
    for line in reversed(data):
        entry = parse(line)
        if not entry.pid:
            logger.debug("Skip broken entry: '%s'", line)
            continue

        if entry.cmd == 'add':
            if entry.pid in dropped:
                dropped.remove(entry.pid)
            else:
                if func(entry):
                    limit -= 1
        elif entry.cmd == 'drop':
            dropped.add(entry.pid)

        if limit == 0:
            logger.debug("User core processing reached limit")
            break


def process_user_cores(filename, suite, gdb_path, out_dir, core_limit, store_cores):
    if not gdb_path or not os.path.exists(gdb_path):
        logger.debug("Cannot process user cores - no gdb provided")

    with open(filename) as afile:
        data = afile.readlines()
        logger.debug("Number of registered commands: %d core processing limit: %d", len(data), core_limit)

    def resolve(entry):
        binar_name = os.path.basename(entry.binary_path)
        bt = shared.postprocess_coredump(
            entry.binary_path,
            entry.cwd,
            entry.pid,
            suite.chunk.logs,
            gdb_path,
            store_cores,
            binar_name,
            out_dir,
        )

        if bt:
            suite.add_chunk_error(
                "[[bad]]Found core for user process [[imp]]{}[[bad]]\n{}".format(
                    binar_name, cores.colorize_backtrace(bt)
                )
            )
            return True

    parse_core_lines(data, resolve, core_limit)
