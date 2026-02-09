import collections
import contextlib
import functools
import itertools
import json
import logging
import os
import io
import re
import shutil
import stat
import subprocess
import sys
import threading
import time

import six

import exts.tmp
from devtools.ya.test import const
from devtools.ya.test.util import tools
from devtools.ya.test.system import process

from library.python import cores
from library.python import eq_interval_sampling

from yalibrary import formatter
from yalibrary import term
from yalibrary.loggers.file_log import TokenFilterFormatter
from yalibrary.term import console


logger = logging.getLogger(__name__)


def setup_logging(
    level,
    log_path,
    status_logger_names=None,
    extra_file_loggers=None,
    fmt="%(asctime)-15s %(levelname)s %(name)s: %(message)s",
):
    root_logger = logging.getLogger()
    root_logger.handlers = []
    root_logger.setLevel(logging.DEBUG)

    if log_path:
        handler = tools.get_common_logging_file_handler(log_path, mode="w")
        root_logger.addHandler(handler)
        for logger_ in extra_file_loggers or []:
            logger_.addHandler(handler)

    handler = logging.StreamHandler(sys.stderr)
    handler.setFormatter(TokenFilterFormatter(fmt))
    handler.setLevel(level)
    root_logger.addHandler(handler)

    if status_logger_names:
        for logger_name in status_logger_names:
            handler = logging.StreamHandler(sys.stderr)
            handler.setFormatter(TokenFilterFormatter("##status##%(message)s"))
            handler.setLevel(logging.DEBUG)
            logging.getLogger(logger_name).handlers = [handler]


def dump_dir_tree(root=None, stream=None, logger_func=None, header=None):
    root = root or os.getcwd()
    tree = collections.defaultdict(list)

    class Entry(object):
        def __init__(self, name, filename, size, dir=False):
            self.name = name
            self.size = size
            self.filename = filename
            if os.path.islink(filename):
                self.symlink = os.readlink(filename)
            else:
                self.symlink = None
            self.dir = dir

        def dumps(self):
            if self.symlink:
                suffix = '-> {} ({})'.format(
                    self.symlink, 'file is missing' if self.size is None else formatter.format_size(self.size)
                )
            elif self.dir:
                suffix = '[{} {}]'.format(len(tree.get(self.filename, [])), formatter.format_size(self.size))
            else:
                suffix = '({})'.format(formatter.format_size(self.size))
            return "{} {}".format(self.name, suffix)

    def size_sum(path):
        return sum(e.size for e in tree[path] if e.size)

    def iter_entries(x):
        return sorted(x, key=lambda y: y.lower())

    for path, dirs, files in os.walk(root, topdown=False):
        path = os.path.relpath(path, root)
        if path == '.':
            path = ''

        for d in iter_entries(dirs):
            entry = Entry(d, os.path.join(path, d), size_sum(os.path.join(path, d)), dir=True)
            tree[path].append(entry)

        for f in iter_entries(files):
            filename = os.path.join(root, path, f)
            size = os.path.getsize(filename) if os.path.exists(filename) else None
            entry = Entry(f, os.path.join(path, f), size)
            tree[path].append(entry)

    lines = [Entry(root, '', size_sum(''), dir=True).dumps()]

    def fmt(entry, prefix, last_entry):
        line_prefix = prefix + ("└─ " if last_entry else "├─ ")
        lines.append("{}{}".format(line_prefix, entry.dumps()))
        if tree[entry.filename]:
            dump(entry.filename, prefix + ("   " if last_entry else "│  "))

    def dump(path, prefix):
        if path not in tree:
            return

        for child in tree[path][:-1]:
            fmt(child, prefix, False)

        child = tree[path][-1]
        fmt(child, prefix, True)

    dump('', '')

    msg = '{}{}'.format(header or 'Test environment:\n', '\n'.join(lines))

    if stream:
        stream.write(msg + '\n')
    elif logger_func:
        logger_func(msg)
    else:
        logger.debug(msg)


def get_projects_from_file(file_path):
    if os.path.exists(file_path):
        with open(file_path) as projects_f:
            return [_f for _f in projects_f.read().split() if _f]
    logger.debug("%s does not exist, assuming that project list is empty", file_path)
    return []


def change_cmd_root(cmd_args, old_root, new_root, build_root, skip_list=None, skip_args=None):
    changed_cmd = []
    for arg in cmd_args:
        if skip_args and changed_cmd and changed_cmd[-1] in skip_args:
            changed_cmd.append(arg)
        else:
            changed_cmd.append(change_root(arg, old_root, new_root, build_root, skip_list))
    return changed_cmd


def change_root(where, old_root, new_root, build_root, skip_list=None):
    if skip_list and where in skip_list:
        return where
    else:
        # Provide replace function instead of using string to avoid backslash escaping
        # python3:
        # >>> re.sub(re.escape(r'c:\h'), r'c:\d', r'c:\h\3')
        # Traceback (most recent call last):
        #   File "/usr/lib/python3.8/sre_parse.py", line 1039, in parse_template
        #     this = chr(ESCAPES[this][1])
        # KeyError: '\\d'
        #
        # python2:
        # >>> re.sub(re.escape(r"c:\h"), r"c:\d", r"c:\h\3")
        # 'c:\\d\\3'
        def repl(_):
            return new_root

        # replace only first encounter
        return re.sub(re.escape(old_root), repl, where, count=1)


def build_filter_message(description, test_name_filters, empty_suites_count):
    result = []
    mtype = "warn"
    if description:
        msg = re.sub(r"(\d+)", r"[[imp]]\1[[{}]]".format(mtype), description)
        result.append(msg)
    if test_name_filters:
        msg = "by filter{} {}".format("" if len(test_name_filters) == 1 else "s", ", ".join(test_name_filters))
        if empty_suites_count:
            msg += ": [[imp]]{}[[{}]] suites removed".format(empty_suites_count, mtype)
        result.append(msg)

    if result:
        return "[[{}]]Number of suites {}[[rst]]".format(mtype, ", ".join(result))
    return None


def get_common_test_outputs(out_dir):
    return {
        "meta": os.path.join(out_dir, 'meta.json'),
        "trace": os.path.join(out_dir, const.TRACE_FILE_NAME),
        "run_test_log": os.path.join(out_dir, "run_test.log"),
    }


def get_sanitizer_first_error(filename):
    san_found = re.compile(r"(: ([A-Z][\w]+Sanitizer)|: runtime error:)")
    san_summary = re.compile(r"SUMMARY: ([A-Z][\w]+Sanitizer)")
    inframe = re.compile(r"^\s+\S")

    res = []
    with io.open(filename, errors='ignore', encoding='utf-8') as afile:
        line = ''
        try:
            while not san_found.search(line):
                line = next(afile)
            # Save first error block
            while not inframe.search(line):
                res.append(line)
                line = next(afile)
            while inframe.search(line):
                res.append(line)
                line = next(afile)

            while not san_summary.search(line):
                line = next(afile)
            # Save tail
            res.append(line)
            while True:
                res.append(next(afile))
        except StopIteration:
            pass

    return "".join(res)


def get_valgrind_error_summary(filename):
    valgrind_summary = re.compile(r" HEAP SUMMARY:")
    stack = []
    found = False

    with open(filename) as afile:
        for line in afile:
            if found:
                stack.append(line)
                continue

            if valgrind_summary.search(line):
                stack.append(line)
                found = True

    return "".join(stack)


def colorize_sanitize_errors(text):
    lines = []
    colorized = False
    for line in text.split('\n'):
        if line.startswith('    #'):
            lines.append('    ' + cores.colorize_backtrace(line.lstrip(' ')))
            colorized = True
        else:
            if colorized:
                line = '[[bad]]' + line
                colorized = False
            lines.append(line)
    return '\n'.join(lines)


def clean_ansi_escape_sequences(text):
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    return ansi_escape.sub('', text)


def dump_trace_file(suite, filename):
    if suite:
        logger.debug("Dumping tracefile")
        suite.generate_trace_file(filename, append=True)
        logger.debug("Tracefile dumped")


def get_test_name(t, limit=256):
    if len(t.name) > limit:
        return t.name[:limit] + "..."
    return t.name


def adjust_test_status(
    suite,
    exit_code,
    testing_finished=0,
    timeout=0,
    attempt_num=0,
    stderr="",
    stdout="",
    add_error_func=None,
    propagate_timeout_info=False,
):
    # warn: method isn't idempotent
    # TODO add_suite_error -> add_chunk_error ??
    add_error_func = add_error_func or suite.add_suite_error

    if exit_code == const.TestRunExitCode.TimeOut:
        if suite.chunks:
            last_crashed_case = None
            launched_tests = []
            not_launched_tests = []

            for chunk in suite.chunks:
                for test_case in chunk.tests:
                    if test_case.status == const.Status.NOT_LAUNCHED:
                        not_launched_tests.append(test_case)
                        test_case.comment = '[[bad]]Test was not launched, because other tests have used all allotted time ({} s)'.format(
                            timeout
                        )
                        continue
                    elif test_case.status == const.Status.CRASHED:
                        last_crashed_case = test_case
                    launched_tests.append(test_case)

                if last_crashed_case:
                    last_crashed_case.status = const.Status.TIMEOUT
                    if not last_crashed_case.elapsed:
                        assert testing_finished
                        assert testing_finished > last_crashed_case.started, (
                            last_crashed_case,
                            testing_finished,
                            last_crashed_case.started,
                        )
                        last_crashed_case.elapsed = testing_finished - last_crashed_case.started

                    last_crashed_case.comment = '[[bad]]Killed by timeout ({} s)'.format(timeout)
                    if attempt_num:
                        last_crashed_case.comment += " during attempt #{} ".format(attempt_num + 1)
                if launched_tests:
                    lines = []
                    listing_limit = 10
                    sorted_tests = sorted(launched_tests, key=lambda x: x.elapsed, reverse=True)

                    for test_case in sorted_tests[:listing_limit]:
                        status = const.Status.TO_STR[test_case.status]
                        lines += [
                            "[[unimp]]{} ([[{}]]{}[[unimp]]) duration: [[rst]]{:.2f}s".format(
                                get_test_name(test_case), const.StatusColorMap[status], status, test_case.elapsed
                            )
                        ]

                    if len(sorted_tests) > listing_limit:
                        duration = sum(t.elapsed for t in sorted_tests[listing_limit:])
                        lines.append(
                            "{} more tests with {:.2f}s total duration are not listed.".format(
                                len(sorted_tests) - listing_limit, duration
                            )
                        )

                    parts = ["[[bad]]List of the tests involved in the launch:"]
                    parts.extend(lines)

                    nskipped = len(not_launched_tests)
                    if nskipped == 1:
                        parts.append(
                            "[[unimp]]{}[[bad]] test was not launched inside chunk.".format(not_launched_tests[0])
                        )
                    elif nskipped > 1:
                        parts.append("[[imp]]{}[[bad]] tests were not launched inside chunk.".format(nskipped))

                    msg = "\n".join(parts)

                    if propagate_timeout_info:
                        if last_crashed_case:
                            last_crashed_case.comment += "\n" + msg

                        for test_case in not_launched_tests:
                            test_case.comment += msg

                    # Dump list of the involved tests to the chunk's snippet
                    add_error_func(msg, const.Status.TIMEOUT)

        statuses = [test_case.status for chunk in suite.chunks for test_case in chunk.tests]
        if statuses.count(const.Status.TIMEOUT) == 0:
            add_error_func('[[bad]]Killed by timeout ({} s)'.format(timeout), const.Status.TIMEOUT)

    # there may be empty suites if filter is specified - rely on exit_code to find failed
    if not suite.tests and exit_code not in [0, const.TestRunExitCode.TimeOut]:
        error = "Run failed with exit code {}".format(exit_code)
        details = stderr.strip() + stdout.strip()
        if details:
            error += "\n" + details
        add_error_func("[[bad]]{}[[rst]]".format(error))


def read_tail(filename, size):
    with io.open(filename, "r", errors='ignore', encoding='utf-8') as afile:
        afile.seek(0, os.SEEK_END)
        flen = afile.tell()
        afile.seek(flen - min(flen, size), os.SEEK_SET)
        return afile.read()


def get_tiny_tail(filename, size=2048, nlines=10):
    data = read_tail(filename, size)
    if data.count('\n') > nlines:
        data = '\n'.join(data.split('\n')[-nlines:])
    return data


def concatenate_files(files, dst, max_file_size=0, before_callback=None, after_callback=None):
    logger.debug('Concatenate %d files to %s', len(files), dst)
    partition_len = max_file_size // len(files)
    half_partition = partition_len // 2
    buff_len = 16 * 1024

    if max_file_size:
        logger.debug('Concatenated files will be truncated with partition_len: %d', partition_len)

    def copyfileobj(fsrc, fdst, lenght):
        done = 0
        while done < lenght:
            buf = fsrc.read(buff_len)
            if not buf:
                break
            fdst.write(buf)
            done += len(buf)

    with open(dst, "wb") as dstfile:
        for filename in files:
            if not os.path.isfile(filename):
                logger.warning('%s is not a regular file', filename)
                continue
            if max_file_size:
                with open(filename, "rb") as afile:
                    afile.seek(0, os.SEEK_END)
                    file_len = afile.tell()
                    afile.seek(0, os.SEEK_SET)

                    if before_callback:
                        before_callback(filename, dstfile)

                    copyfileobj(afile, dstfile, half_partition)

                    if file_len // 2 > half_partition:
                        afile.seek(file_len - half_partition, os.SEEK_SET)
                        dstfile.write(b"\n..[truncated]..\n")

                    copyfileobj(afile, dstfile, half_partition)

                    if after_callback:
                        after_callback(filename, dstfile)
            else:
                with open(filename, "rb") as file:
                    shutil.copyfileobj(file, dstfile)


def run_under_gdb(cmd, gdb_path, tty='/dev/tty'):
    # TODO keep args in sync with run_with_gdb() from devtools/ya/test/programs/test_tool/run_test/run_test.py untill YA-724 is done
    extra_args = [gdb_path, '-iex', 'set demangle-style none', '-ex', 'set demangle-style auto']
    source_root = os.environ.get("ORIGINAL_SOURCE_ROOT")
    if source_root:
        extra_args += ["-ex", "set substitute-path /-S/ {}/".format(source_root)]
    extra_args += ["-ex", "set filename-display absolute", "--args"]
    cmd = extra_args + cmd
    additional_env = {"TERMINFO": os.path.abspath(os.path.join(gdb_path, "..", "..", "lib/terminfo"))}
    return popen_tty(cmd, tty, additional_env)


def run_under_dlv(cmd, dlv_path, tty='/dev/tty', dlv_args=None):
    dlv_args = dlv_args or []
    cmd = [dlv_path, "exec", cmd[0]] + dlv_args + ["--"] + cmd[1:]
    return popen_tty(cmd, tty)


def popen_tty(cmd, tty, additional_env=None):
    env = os.environ
    if additional_env is not None:
        env = os.environ.copy()
        env.update(additional_env)
    if not tty:
        return subprocess.Popen(cmd, env=env)
    old_attrs = term.console.connect_real_tty(tty)
    try:
        return subprocess.Popen(cmd, env=env)
    finally:
        term.console.restore_referral(*old_attrs)


def get_cgroup_available_memory_in_mb():
    with open('/proc/self/cgroup') as afile:
        data = afile.read()

    logger.debug("/proc/self/cgroup:\n%s", data)

    limit, used = None, None
    for line in data.split('\n'):
        if not line:
            continue
        _, controllers, pathname = line.strip().split(':', 2)
        if controllers != "memory":
            continue

        try:
            with open("/sys/fs/cgroup/{}/{}/memory.limit_in_bytes".format(controllers, pathname)) as afile:
                limit = int(afile.read())
            with open("/sys/fs/cgroup/{}/{}/memory.usage_in_bytes".format(controllers, pathname)) as afile:
                used = int(afile.read())
        except Exception as e:
            logger.debug("failed to get memory limit: %s", e)
        logger.debug("CGroup '%s' memory.limit_in_bytes: %s memory.usage_in_bytes: %s", pathname, limit, used)

    if limit and used:
        return limit - used
    return None


def get_available_memory_in_mb():
    """
    don't use psutil - it's currently doesn't build on windows
    and require GLIBC_2.13 or later (won't work on lucid)
    """
    cgroup_avilable = get_cgroup_available_memory_in_mb()

    with open('/proc/meminfo') as afile:
        data = afile.read()

    logger.debug("/proc/meminfo:\n%s", data)

    free = buffers = cached = available = None
    for line in data.split("\n"):
        if line.startswith("MemAvailable:"):
            available = int(line.split()[1]) // 1024
            break
        # old linux versions may not contain MemAvailable field
        elif line.startswith("MemFree:"):
            free = int(line.split()[1]) // 1024
        elif line.startswith("Buffers:"):
            buffers = int(line.split()[1]) // 1024
        elif line.startswith("Cached:"):
            cached = int(line.split()[1]) // 1024

    available = available or free + buffers + cached
    # processes is launched within cgroup with memory limit
    if cgroup_avilable and cgroup_avilable < available:
        return cgroup_avilable
    return available


def timeit(func):
    if not hasattr(timeit, 'stat'):
        setattr(timeit, 'stat', collections.defaultdict(int))
        setattr(timeit, 'lock', threading.Lock())
    stat = getattr(timeit, 'stat')
    lock = getattr(timeit, 'lock')

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        start = time.time()
        res = func(*args, **kwargs)
        duration = time.time() - start
        with lock:
            stat[func.__name__] += duration
        return res

    return wrapper


def get_testcases_info(cmd_result):
    result = []
    if cmd_result.exit_code == 0:
        for line in cmd_result.std_out.split('\n'):
            line = line.strip()
            if not line:
                continue

            try:
                test_obj = json.loads(line)
                if 'nodeid' in test_obj:
                    nodeid = test_obj['nodeid']
                    if const.TEST_SUBTEST_SEPARATOR in nodeid:
                        testname, testcase = nodeid.split(const.TEST_SUBTEST_SEPARATOR, 1)
                        result.append((testname, testcase))
                    else:
                        result.append((nodeid, ""))
                    continue
            except (json.JSONDecodeError, ValueError):
                pass

            # Fallback to old format, "suite::test" string
            if const.TEST_SUBTEST_SEPARATOR in line:
                testname, testcase = line.split(const.TEST_SUBTEST_SEPARATOR, 1)
                result.append((testname, testcase))

        return result
    raise Exception(cmd_result.std_err)


PY_SOURCES_EXTRACTOR = '''
import re
import os
import sys
import hashlib
from library.python import resource

output = '{output}'
match = re.compile(r'{pattern}').search

for filename in resource.resfs_files():
    if match(filename):
        abspath = os.path.join(output, filename)
        try:
            os.makedirs(os.path.dirname(abspath))
        except OSError:
            pass
        content = resource.resfs_read(filename, builtin=True)
        with open(abspath, 'wb') as afile:
            afile.write(content)
        print('%s=%s' % (filename, hashlib.md5(content).hexdigest()))
'''


def extract_py_programs_sources(binaries, outdir, pattern='.*'):
    for binary in binaries:
        assert os.path.exists(binary), binary

    script_path = exts.tmp.create_temp_file()
    with open(script_path, 'w') as afile:
        afile.write(PY_SOURCES_EXTRACTOR.format(output=outdir, pattern=pattern))

    checksum = {}
    for binary in binaries:
        for line in subprocess.check_output([binary, script_path], env={'Y_PYTHON_ENTRY_POINT': ':main'}).split():
            filename, hashval = line.split(b'=', 1)
            if filename in checksum:
                assert checksum[filename]['hash'] == hashval, "Content of '{}' differs. Binaries: {}, {}".format(
                    filename, binary, checksum[filename]['binary']
                )
            else:
                checksum[filename] = {
                    'hash': hashval,
                    'binary': binary,
                }


CYTHON_INCLUDE_MAP_EXTRACTOR = '''
import sys
import json
import run_import_hook
from library.python import resource

map = dict()

if sys.version_info[0] == 2:
    utf_8_decode = lambda x: x
else:
    utf_8_decode = lambda x: x.decode('utf-8', 'ignore')

for filename, includes in resource.iteritems('resfs/cython/include/', strip_prefix=True):
    for include in utf_8_decode(includes).split(':'):
        map[include] = filename
    with open('{output}', 'w') as afile:
        json.dump(map, afile)
'''


def create_cython_include_map(binaries, filename):
    for binary in binaries:
        assert os.path.exists(binary), binary

    script_path = exts.tmp.create_temp_file()
    with open(script_path, 'w') as afile:
        afile.write(CYTHON_INCLUDE_MAP_EXTRACTOR.format(output=filename))

    include_map = {}

    for binary in binaries:
        subprocess.check_call([binary, script_path], env={'Y_PYTHON_ENTRY_POINT': ':main'})
        if os.path.exists(filename):
            with open(filename) as afile:
                include_map.update(json.load(afile))

    with open(filename, 'w') as afile:
        json.dump(include_map, afile)


def extract_cython_entrails(binaries, source_dir, include_map_file):
    c_exts = ['.c', '.cpp']
    # TODO ('', '.py3', '.py2') -> ('.py3', '.py2'), when https://a.yandex-team.ru/review/3511262 is merged
    suffixes = [''.join(x) for x in itertools.product(('.pyx',), ('', '.py3', '.py2'), c_exts)]
    suffixes += c_exts
    suffixes += ['.pxi', '.pyx']

    pattern = '({})$'.format('|'.join(re.escape(x) for x in suffixes))

    extract_py_programs_sources(binaries, source_dir, pattern)
    create_cython_include_map(binaries, include_map_file)


def archive_postprocess_unlink_files(src, dst, st_mode):
    if stat.S_ISREG(st_mode):
        try:
            os.unlink(src)
        except OSError as e:
            logger.debug("Failed to unlink %s: %s", src, e)


@exts.func.lazy
def setup_coredump_filter():
    # XXX reduce noise from core_dumpfilter
    logging.getLogger("sandbox.sdk2.helpers.coredump_filter").setLevel(logging.ERROR)


def postprocess_coredump(binary, cwd, pid, logs, gdb_path, collect_cores, filename, logsdir):
    core_path = cores.recover_core_dump_file(binary, cwd, pid)
    if core_path:
        if collect_cores:
            core_filename = "{}.core".format(filename)
            # Copy core dump file, because it may be overwritten
            new_core_path = os.path.join(logsdir, core_filename)

            logger.debug("Coping core dump file from '%s' to the '%s'", core_path, new_core_path)
            try:
                shutil.copyfile(core_path, new_core_path)
            except IOError as e:
                logger.warning("Failed to copy core dump: %s", e)
                return

            core_path = new_core_path

            logs['core'] = core_path
            logs['binary'] = binary

        if os.path.exists(gdb_path):
            from library.python import coredump_filter

            logger.debug("Obtaining backtrace")
            backtrace = cores.get_gdb_full_backtrace(binary, core_path, gdb_path)

            sys.stderr.write("{}".format(cores.colorize_backtrace(backtrace)))
            stderr_filename = logs.get('stderr')
            if stderr_filename:
                with open(stderr_filename, 'a') as afile:
                    afile.write("{}".format(backtrace))

            bt_filename = os.path.join(logsdir, "{}.backtrace".format(filename))
            with open(bt_filename, "w") as afile:
                afile.write(backtrace)
            logs['backtrace'] = bt_filename

            setup_coredump_filter()
            logger.debug("Generating pretty html version of backtrace aka Tri Korochki")
            pbt_filename = bt_filename + ".html"
            try:
                with open(pbt_filename, "w") as afile:
                    coredump_filter.filter_stackdump(bt_filename, stream=afile)
            except Exception as e:
                logger.exception("Failed to generate html of backtrace: %s", e)
            else:
                logs['backtrace html'] = pbt_filename

            # Drop useless repeated messages
            # [New Thread 0x41e02940 (LWP 25582)]
            # [New LWP 623332]
            # etc
            return re.sub(r"^\[New[^\]]+LWP[^\]]+\]\n", '', backtrace, flags=re.MULTILINE)


def get_oauth_token(args):
    token = getattr(args, 'token')
    if token:
        return token

    token_path = getattr(args, 'token_path')
    if token_path:
        with open(token_path, 'r') as f:
            token = f.read().strip()

        return token

    return None


def get_oauth_token_options(opts, test_tool_mode=False):
    if test_tool_mode:
        token = getattr(opts, "token", None)
        token_path = getattr(opts, "token_path", None)
        return ['--token-path', token_path] if token_path else (['--token', token] if token else [])

    import devtools.ya.core.config as cc

    oauth_token = getattr(opts, "oauth_token", None)
    if not oauth_token:
        oauth_token = getattr(opts, "sandbox_oauth_token", None)

    cmd = []
    if oauth_token:
        cmd += ['--token-path', "$(YA_TOKEN_PATH)"] if cc.is_self_contained_runner3(opts) else ['--token', oauth_token]

    return cmd


def tee_execute(cmd, out, err, strip_ansi_codes=False, on_timeout=None, on_startup=None):
    def tee(stop_ev, istream, fouts):
        while True:
            line = istream.readline()
            if line:
                for fout in fouts:
                    fout(line)
            elif stop_ev.is_set():
                break

    def writer(stream, strip=False, lock=None):
        if strip:

            def w(d):
                stream.write(console.strip_ansi_codes(d))
                stream.flush()

            write = w
        else:

            def w(d):
                stream.write(d)
                stream.flush()

            write = w

        if lock:

            def w(d):
                with lock:
                    write(d)

            return w
        else:
            return lambda d: write(d)

    def start_tee_thread(*args):
        th = threading.Thread(target=tee, args=args)
        th.daemon = True
        th.start()
        return th

    def run(stdout, stderr):
        logger.debug("cmd: %s, env: %s", cmd, os.environ)
        res = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=os.environ)

        if on_startup:
            on_startup(res)

        lock = threading.Lock()
        stop_ev = threading.Event()
        terr, tout = None, None
        exit_code = None
        try:
            sys_stderr = sys.stderr
            sys_stdout = sys.stdout
            if hasattr(sys.stdout, 'buffer'):
                sys_stderr = sys.stderr.buffer
                sys_stdout = sys.stdout.buffer
            terr = start_tee_thread(
                stop_ev, res.stderr, [writer(sys_stderr, lock=lock), writer(stderr, strip=strip_ansi_codes)]
            )
            tout = start_tee_thread(
                stop_ev, res.stdout, [writer(sys_stdout, lock=lock), writer(stdout, strip=strip_ansi_codes)]
            )
            res.wait()
        except process.SignalInterruptionError as e:
            e.res = res
            if on_timeout:
                exit_code = on_timeout(res)
            raise
        finally:
            if exit_code is not None:
                res.terminate()
                res.wait()

            logger.debug("res.returncode: %s", res.returncode)
            stop_ev.set()
            if terr:
                terr.join()
            if tout:
                tout.join()
        return res

    @contextlib.contextmanager
    def open_stream(entry):
        if isinstance(entry, six.string_types):
            stream = open(entry, "wb")
            yield stream
            stream.close()
        else:
            yield entry

    with open_stream(err) as stream_err:
        with open_stream(out) as stream_out:
            return run(stream_out, stream_err)


def limit_tests(container, limit):
    assert limit > 0, limit
    tests = container.tests

    if len(tests) <= limit:
        return

    samples = eq_interval_sampling.eq_interval_sampling(tests, nsamples=20)

    msg = (
        "[[bad]]The run contains [[imp]]{}[[bad]] test cases, which exceeds the allowed limit of [[imp]]{}.[[bad]]"
        " You may have errors in the test case generation function or"
        " an undesirably huge test set that should be split into several ya.make files."
        " Some test names provided below.[[rst]]\n"
    ).format(len(tests), limit)
    msg += '\n'.join(' - ' + str(test) for test in samples)

    container.remove_tests()
    container.add_error(msg)
