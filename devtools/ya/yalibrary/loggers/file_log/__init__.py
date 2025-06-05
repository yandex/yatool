import datetime
import logging
import os
import psutil
import re
import resource
import six
import sys

from exts import fs
from exts import func
from exts.compress import UCompressor


_LOG_FILE_NAME_FMT = '%H-%M-%S'
_LOG_DIR_NAME_FMT = '%Y-%m-%d'
_DAYS_TO_SAVE = 7
FILE_LOG_FMT = '%(asctime)s %(memUsage)s%(levelname)s (%(name)s) [%(threadName)s] %(message)s'


class TokenFilterFormatter(logging.Formatter):
    def __init__(self, fmt, replacements=None, add_mem_usage=False):
        super(TokenFilterFormatter, self).__init__(fmt)
        self._replacements = replacements or []
        self._add_mem_usage = add_mem_usage
        if not replacements:
            import devtools.ya.core.sec as sec

            self._replacements = sec.mine_suppression_filter()

    def _filter(self, s):
        for r in self._replacements:
            s = s.replace(r, "[SECRET]")
        return s

    def format(self, record):
        record.memUsage = self._mem_usage()
        return self._filter(super(TokenFilterFormatter, self).format(record))

    def _mem_usage(self):
        if not self._add_mem_usage:
            return ""
        rss_bytes = psutil.Process().memory_info().rss
        max_rss_kb = self._vmhwm() or resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        return "{:.3f}/{:.3f} GB ".format(rss_bytes / (1024.0**3), max_rss_kb / (1024.0**2))

    def _vmhwm(self):
        if not sys.platform.startswith("linux"):
            return
        line = None
        status_file = "/proc/self/status"
        with open(status_file) as f:
            for s in f:
                if s.startswith("VmHWM:"):
                    line = s
                    break
        if not line:
            return
        # JFI: 'kB' suffix is hardcoded in the Linux kernel source file fs/proc/task_mmu.c
        m = re.match(r"VmHWM:\s*(\d+)\s*kB", line)
        if m:
            return int(m.group(1))


class LogChunks(object):
    def __init__(self, log_dir):
        self._log_dir = fs.create_dirs(log_dir)

    def get_or_create(self, chunk_name):
        return fs.create_dirs(os.path.join(self._log_dir, chunk_name))

    def cleanup(self, predicate):
        for x in os.listdir(self._log_dir):
            if predicate(x):
                fs.remove_tree_safe(os.path.join(self._log_dir, x))


class UCFileHandler(logging.StreamHandler):
    def __init__(self, filename, codec):
        mode = "wt" if six.PY3 else "wb"

        self.compressor = UCompressor(filename, codec, mode)
        self.compressor.start()
        logging.StreamHandler.__init__(self, self.compressor.getInputStream())

    def close(self):
        self.acquire()
        try:
            try:
                try:
                    self.flush()
                finally:
                    self.compressor.stop()
            finally:
                logging.StreamHandler.close(self)
        finally:
            self.release()

    def emit(self, record):
        logging.StreamHandler.emit(self, record)


def parse_log_dir(x):
    try:
        return datetime.datetime.strptime(x, _LOG_DIR_NAME_FMT)
    except ValueError:
        return None


@func.memoize()
def log_creation_time(run_uid):
    # type: (str) -> datetime.datetime
    return datetime.datetime.now()


def format_date(dt):
    # type: (datetime.datetime) -> str
    return dt.strftime(_LOG_DIR_NAME_FMT)


def format_time(dt):
    # type: (datetime.datetime) -> str
    return dt.strftime(_LOG_FILE_NAME_FMT)


def with_file_log(log_dir, run_uid):
    now = log_creation_time(run_uid)

    chunks = LogChunks(log_dir)

    root = logging.getLogger()
    root.setLevel(logging.DEBUG)

    def older_than(x):
        return now - x > datetime.timedelta(days=_DAYS_TO_SAVE) if x else False

    chunks.cleanup(func.compose(older_than, parse_log_dir))

    log_chunk = chunks.get_or_create(format_date(now))
    file_name = os.path.join(log_chunk, format_time(now) + '.' + run_uid + '.log')
    root.addHandler(_file_logger(file_name))

    yield file_name


def with_custom_file_log(ctx, params, replacements):
    root = logging.getLogger()

    log_file = getattr(params, 'log_file', None)
    in_memory_handler = ctx.file_in_memory_log

    if log_file:
        file_handler = _file_logger(log_file, replacements=replacements)

        for entry in in_memory_handler.storage:
            file_handler.emit(entry)
        root.addHandler(file_handler)

    in_memory_handler.close()
    root.removeHandler(in_memory_handler)

    yield


def _file_logger(log_file, loglevel=logging.DEBUG, replacements=None):
    if log_file.endswith('.uc'):
        file_handler = UCFileHandler(log_file, 'zstd_1')
    else:
        file_handler = logging.FileHandler(log_file)

    file_handler.setLevel(loglevel)

    file_handler.setFormatter(
        TokenFilterFormatter(FILE_LOG_FMT, replacements or [], add_mem_usage=bool(os.getenv("YA_LOG_MEM_USAGE")))
    )

    return file_handler
