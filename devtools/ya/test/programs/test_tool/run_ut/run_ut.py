# coding: utf-8

from __future__ import print_function
import io
import os
import re
import six
import sys
import copy
import json
import time
import uuid
import errno
import signal
import logging
import argparse
import traceback
import threading
import subprocess
import collections
import multiprocessing
import concurrent.futures as futures

from six import string_types

import library.python.cores as cores

from yatest.common import process
from yatest_lib import test_splitter
import exts.fs
import exts.tmp
from devtools.ya.test.util import tools, shared
from devtools.ya.test.common import get_test_log_file_path
from devtools.ya.test import facility
from devtools.ya.test.const import Status, TEST_BT_COLORS
from devtools.ya.test.filter import make_testname_filter
from devtools.ya.test.test_types.common import PerformedTestSuite
from yalibrary import display

logger = logging.getLogger(__name__)


MAX_FILE_SIZE = 1024 * 1024  # 1MiB
TIMEOUT_RC = 98
VALGRIND_ERROR_RC = 101
START_MARKER = "###subtest-started:"
FINISH_MARKER = "###subtest-finished:"
SHUTDOWN_REQUESTED = False
LOCK = threading.Lock()

Result = collections.namedtuple(
    'Result', ['rc', 'pid', 'logs', 'last_test_name', 'first_test_started', 'end_time', 'stderr_file', 'stdout_file']
)

WINE_CWD_DRIVE = "t:"
WINE_CWD_PATH = WINE_CWD_DRIVE + "\\cwd\\"
WINE_EXE_DRIVE = "e:"
WINE_BUILD_ROOT_DRIVE = "b:"


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--binary", required=True, help="Path to the unittest binary")
    parser.add_argument("-t", "--trace-path", help="Path to the output trace log")
    parser.add_argument("-o", "--output-dir", help="Path to the output dir")
    parser.add_argument(
        "-f", "--test-filter", default=[], action="append", help="Run only specified tests (binary name or mask)"
    )
    parser.add_argument("-p", "--project-path", help="Project path relative to arcadia")
    parser.add_argument("--modulo", default=1, type=int)
    parser.add_argument("--modulo-index", default=0, type=int)
    parser.add_argument("--partition-mode", default='SEQUENTIAL', help="Split tests according to partitoin mode")
    parser.add_argument("--split-by-tests", action='store_true', help="Split test execution by tests instead of suites")
    parser.add_argument("--test-list", action="store_true", help="List of tests")
    parser.add_argument(
        "--list-timeout", default=0, type=int, help="Specifies time limit in seconds to list tests default:%(default)ss"
    )
    parser.add_argument(
        "--sequential-launch",
        action="store_true",
        help="Force script to launch tests one by one, instead of class by class",
    )
    parser.add_argument(
        "--truncate-logs",
        action="store_true",
        help="Truncate logs. Don't collect core dump files. Care about autocheck",
    )
    parser.add_argument(
        "--dont-store-cores", dest='collect_cores', action="store_false", help="Don't collect core dump files"
    )
    parser.add_argument("--valgrind-path", dest="valgrind_path", action='store', default='')
    parser.add_argument("--with-wine", dest="with_wine", action='store_true', default=False)
    parser.add_argument("--wine-path", dest="wine_path", action='store', default='')
    parser.add_argument("--gdb-path", dest="gdb_path", action='store', default='')
    parser.add_argument("--gdb-debug", dest="gdb_debug", action='store_true', help="Run ut with gdb")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--test-mode", action="store_true", help="For test purposes")
    parser.add_argument(
        "--test-param",
        action="append",
        default=[],
        dest="test_params",
        help="Arbitrary parameters to be passed to tests",
    )
    parser.add_argument("--test-list-path", help="path to test list calculated in list_node", default=None)
    parser.add_argument("--stop-signal", default=0, type=int)
    parser.add_argument(
        "--test-binary-args", default=[], action="append", help="Transfer additional parameters to test binary"
    )

    parser.add_argument(
        "--parallel-tests-within-node-workers",
        default=None,
        help="Amount of workers to run tests in parallel",
    )
    parser.add_argument(
        '--cpu-per-test-requested',
        help="Amount of CPUs requested by test",
        default=0,
        type=int,
    )
    parser.add_argument('--temp-tracefile-dir', help="Directory to place temporary trace-files", default=None)

    args = parser.parse_args()
    args.binary = os.path.abspath(args.binary)
    if not os.path.exists(args.binary):
        parser.error("Unittest binary doesn't exist: %s" % args.binary)
    if not args.test_list and not args.trace_path:
        parser.error("Path to the trace file must be specified")
    if not args.project_path:
        path = os.path.dirname(args.binary)
        if "arcadia" not in path:
            parser.error("Failed to determine project path")
        args.project_path = path.rsplit("arcadia")[1]
    args.project_path.strip("/")

    # Don't collect core dump files if truncation is required
    if args.truncate_logs:
        args.collect_cores = False
    return args


def smooth_shutdown(signo, frame):
    global SHUTDOWN_REQUESTED
    SHUTDOWN_REQUESTED = True


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def is_darwin():
    import platform

    return platform.system().lower().startswith("darwin")


def filter_tests_by_filter(test_names, filters, binary_name):
    if binary_name in filters:
        return test_names
    return list(filter(make_testname_filter(filters), test_names))


def split_test_name(test_name):
    return test_name.rsplit("::", 1)


def get_test_classes(project_path, binary, test_filter, tracefile, list_timeout, gdb_path, wine_path):
    def dump_error(comment, status):
        suite = gen_suite(project_path)
        suite.add_chunk_error(comment, status)
        shared.dump_trace_file(suite, tracefile)
        raise Exception(comment)

    def on_timeout(result, timeout):
        process._kill_process_tree(result.process.pid, signal.SIGQUIT)

        logging.debug("Waiting for process (%d) to dump core dump file", result.process.pid)
        os.waitpid(result.process.pid, 0)

        result.process_backtrace = ""
        core_path = cores.recover_core_dump_file(result.command[0], os.getcwd(), result.process.pid)
        if core_path:
            result.process_backtrace = cores.get_gdb_full_backtrace(result.command[0], core_path, gdb_path)

    try:
        with exts.tmp.temp_file() as filename:
            cmd = [binary, "--list-verbose", "--list-path"]
            cwd = None
            stdin = None

            if wine_path:
                if os.environ.get("YA_TEST_SHORTEN_WINE_PATH"):
                    local_filename = os.path.basename(filename)
                    if not os.path.exists(local_filename):
                        os.symlink(filename, local_filename)
                    cmd += [WINE_CWD_PATH + local_filename]
                    cmd, cwd, stdin = shorten_wine_paths(wine_path, cmd)
                else:
                    cmd = [wine_path] + cmd + [filename]
            else:
                cmd += [filename]

            process.execute(
                cmd,
                timeout=list_timeout,
                on_timeout=on_timeout,
                collect_cores=False,
                check_sanitizer=False,
                cwd=cwd,
                stdin=stdin,
                text=True,
            )

            result = exts.fs.read_text(filename).strip().split("\n")
            test_names = [_f for _f in result if _f]
            logger.debug("Found tests: '%s'", "' '".join(test_names))
    except process.TimeoutError as e:
        comment = "[[bad]]Cannot obtain list of ut tests in the allotted time ('ut --list-verbose' worked longer than [[imp]]{}s[[bad]])".format(
            list_timeout
        )
        bt = e.execution_result.process_backtrace
        if bt:
            logger.debug("Stack trace for hung process:\n%s", bt)
            comment += "\n{}".format(cores.colorize_backtrace(cores.get_problem_stack(bt), TEST_BT_COLORS))

        sys.stderr.write("Process stderr:\n{}".format(e.execution_result.stderr))
        sys.stdout.write("Process stdout:\n{}".format(e.execution_result.stdout))

        dump_error(comment, Status.TIMEOUT)
    except process.ExecutionError as e:
        result = e.execution_result
        parts = ["[[bad]]Ut test failed with exit code [[imp]]{}[[bad]] while listing tests.".format(result.exit_code)]

        if result.exit_code < 0:
            core_path = cores.recover_core_dump_file(
                e.execution_result.command[0], os.getcwd(), e.execution_result.process.pid
            )
            if core_path:
                bt = cores.get_gdb_full_backtrace(result.command[0], core_path, gdb_path)
                parts.append("Problem thread backtrace:")
                parts.append(cores.colorize_backtrace(cores.get_problem_stack(bt), TEST_BT_COLORS))

        if result.stderr:
            parts.append("Process stderr:")
            parts.append(result.stderr)
        comment = '\n'.join(parts)
        sys.stderr.write(display.strip_markup(comment))
        dump_error(comment, Status.CRASHED)
    except Exception:
        comment = "[[bad]]Internal error while listing tests: {}".format(traceback.format_exc())
        dump_error(comment, Status.INTERNAL)

    if test_filter:
        test_names = filter_tests_by_filter(test_names, test_filter, os.path.basename(binary))
        logger.debug("Tests applying filter (%s): '%s'", test_filter, "' '".join(test_names))

    result = {}
    for name in test_names:
        class_name = split_test_name(name)[0]
        if class_name not in result:
            result[class_name] = [name]
        else:
            result[class_name].append(name)
    return result


def get_duplicates(data):
    seen = set()
    dup = set()
    for i in data:
        if i in seen:
            dup.add(i)
        else:
            seen.add(i)
    return dup


class DeepDict(dict):
    def __getitem__(self, key):
        if key not in self:
            self.__setitem__(key, DeepDict())
        return dict.__getitem__(self, key)


class State(object):
    pretest, started, finished = range(3)


class TestLogExtractor(object):
    def __init__(self, logsdir, ttype):
        self._ttype = ttype
        self._logsdir = logsdir
        self._state = State.pretest
        self._pretest_buffer = []
        self._finished_buffer = []
        self._logs = {}
        self._file = None
        self._test_name = None
        self._first_test_started = False

    def feed(self, line):
        state_changed = self._proceed(line)
        if state_changed:
            # skip writing lines with markers
            return

        if self._state == State.pretest:
            self._pretest_buffer.append(line)
        elif self._state == State.finished:
            self._finished_buffer.append(line)
        elif self._state == State.started:
            assert self._file
            self._file.write(line)

    def flush(self):
        if self._file:
            self._file.flush()

    def release(self):
        self._close_file()

    @property
    def ttype(self):
        return self._ttype

    @property
    def logs(self):
        return self._logs

    @property
    def state(self):
        return self._state

    @property
    def first_test_started(self):
        return self._first_test_started

    @property
    def last_test_name(self):
        return self._test_name

    def _close_file(self):
        if self._file:
            # write output printed after test actual execution
            finished_buffer = six.ensure_text(''.join(self._finished_buffer), errors='ignore', encoding='utf-8')
            self._file.write(finished_buffer)
            self._file.close()
            self._file = None
            self._finished_buffer = []

    def _proceed(self, line):
        if line.startswith(START_MARKER):
            test_name = self._get_test_name(line)
            assert self._test_name != test_name
            self._test_name = test_name
            self._first_test_started = True

            self._close_file()
            extensions = {"stdout": "out", "stderr": "err"}
            # get name from cache or generate new one
            if self._logs.get(test_name):
                filename = self._logs[test_name]
            else:
                filename = get_test_log_file_path(self._logsdir, test_name, extensions[self._ttype])
                self._logs[test_name] = filename

            buffering = 1 if self._ttype == "stderr" else -1
            self._file = io.open(filename, "a", buffering=buffering, encoding='utf-8', errors="ignore")
            logger.debug("Duplicating '%s' test's %s to %s", self._test_name, self._ttype, self._file.name)
            # add premature output to every test
            buffer = six.ensure_text(''.join(self._pretest_buffer), errors='ignore')
            self._file.write(buffer)

            self._state = State.started
            return True
        elif line.startswith(FINISH_MARKER):
            assert self._state != State.pretest, "Unexpectedly came to the finished state from pretest"
            assert self._get_test_name(line) == self._test_name, "Test: {}, Line with marker:{}".format(
                self._test_name, line
            )
            logger.debug("Test '%s' finished (%s)", self._test_name, self._ttype)
            self._test_name = None
            self._state = State.finished
            return True
        return False

    def _get_test_name(self, line):
        if line.startswith(START_MARKER):
            return line[len(START_MARKER) :].strip()
        elif line.startswith(FINISH_MARKER):
            return line[len(FINISH_MARKER) :].strip()
        raise AssertionError("Cannot get test name from '{}'".format(line))


class LogGenerator(object):
    def __init__(self, outfile, errfile, logsdir):
        # log may be not created
        self._utout = io.open(outfile, "a+", errors='ignore', encoding='utf-8')
        # don't read previously recorded data
        self._utout.seek(0, os.SEEK_END)
        self._uterr = io.open(errfile, "a", buffering=1, errors='ignore', encoding='utf-8')
        self._logsdir = logsdir
        self._stdout_extractor = TestLogExtractor(logsdir, "stdout")
        self._stderr_extractor = TestLogExtractor(logsdir, "stderr")

    def saturate(self, stderr_line):
        # store unittest's stderr
        line = six.ensure_text(stderr_line, errors='ignore')
        self._uterr.write(line)
        self._stderr_extractor.feed(line)
        if self._stderr_extractor.state == State.finished:
            self._saturate_stdout()

    def _saturate_stdout(self, until_end=False):
        # don't use fileobject iterator - it always raises StopIteration if it hit EOF once
        while until_end or self._stdout_extractor.state != State.finished:
            line = self._utout.readline()
            if not line:
                break
            # skip empty lines
            if line != '\n':
                self._stdout_extractor.feed(line)
        # instead of close we flush file,
        # because it's may be used to add extra logs after test finished
        self._stdout_extractor.flush()

    def get_logs(self):
        self.release()
        assert self._stdout_extractor.ttype != self._stderr_extractor.ttype
        logs = DeepDict()
        for extractor in [self._stdout_extractor, self._stderr_extractor]:
            for test_name, filename in extractor.logs.items():
                logs[test_name][extractor.ttype] = filename
                logs[test_name]["logsdir"] = self._logsdir
        return logs

    def get_last_test_name(self):
        return self._stderr_extractor.last_test_name

    def is_first_test_started(self):
        return self._stderr_extractor.first_test_started

    def release(self):
        if self._utout:
            # extract last test's stdout
            # because test could crash and no finish marker might be recorded
            self._saturate_stdout(until_end=True)
            self._stdout_extractor.release()
            self._utout.close()
            self._utout = None
        if self._uterr:
            self._stderr_extractor.release()
            self._uterr.close()
            self._uterr = None


def shorten_wine_paths(wine_path, cmd):
    cwd = os.path.abspath(os.getcwd())
    exe = os.path.abspath(cmd[0])

    wineprefix = os.environ.get("WINEPREFIX")
    if wineprefix:
        dosdevices = os.path.join(wineprefix, "dosdevices")
        if not os.path.exists(dosdevices):
            os.makedirs(dosdevices)

        test_drive = os.path.join(dosdevices, WINE_CWD_DRIVE)
        if not os.path.exists(test_drive):
            os.mkdir(test_drive)

        test_drive_cwd = os.path.join(test_drive, "cwd")
        if not os.path.exists(test_drive_cwd):
            os.symlink(cwd, test_drive_cwd)

        tmpdir = os.environ.get("TMPDIR", "")
        wine_tmp = os.path.join(tmpdir, os.path.join(test_drive_cwd, "_wine_tmp"))

        if not os.path.exists(wine_tmp):
            if os.path.exists(tmpdir):
                os.symlink(tmpdir, wine_tmp)
            else:
                os.makedirs(wine_tmp)

        tmpdir = WINE_CWD_PATH + "_wine_tmp\\"

        exe_drive = os.path.join(dosdevices, WINE_EXE_DRIVE)
        if not os.path.exists(exe_drive):
            os.symlink(os.path.dirname(exe), exe_drive)

        shorten_win_exe = WINE_EXE_DRIVE + "\\" + os.path.basename(exe)
        wine_test_context_file = os.path.join(test_drive_cwd, "test.context")

        test_context_file = os.environ.get("YA_TEST_CONTEXT_FILE")
        if test_context_file:
            with open(test_context_file) as f:
                test_context = json.load(f)
            build_root = test_context["runtime"]["build_root"]
            build_root_drive = os.path.join(dosdevices, WINE_BUILD_ROOT_DRIVE)
            if not os.path.exists(build_root_drive):
                os.symlink(build_root, build_root_drive)

            runtime = dict()
            for key, path in test_context["runtime"].items():
                if isinstance(path, string_types):
                    path = path.replace(build_root, WINE_BUILD_ROOT_DRIVE)
                    path = path.replace("/", "\\")
                runtime[key] = path
            test_context["runtime"] = runtime

            with open(wine_test_context_file, "w") as f:
                json.dump(test_context, f, indent=4)

        bat_script = """
            echo on
            set YA_TEST_CONTEXT_FILE=t:\\cwd\\test.context
            set TMPDIR={tmpdir}
            set TMP={tmpdir}
            cd {shorten_win_cwd}
            {cmd}
        """.format(
            shorten_win_cwd=WINE_CWD_PATH,
            cmd=shorten_win_exe + " " + (" ".join(cmd[1:])),
            tmpdir=tmpdir,
        )

        bat_script_file = exts.tmp.temp_file()

        with open(bat_script_file.path, "w") as f:
            f.write(bat_script)

        stdin = open(bat_script_file.path)
        cmd = [wine_path, "cmd"]
        cwd = wineprefix

    else:
        cwd = None
        stdin = None

    return cmd, cwd, stdin


def huge_pipe():
    import fcntl

    rfd, wfd = os.pipe()
    try:
        F_SETPIPE_SZ = 0x407
        fcntl.fcntl(wfd, F_SETPIPE_SZ, 1 << 20)  # 1Mb
    except Exception as e:
        logger.warning("Failed to increase pipe size: %s", e)
    return rfd, wfd


def execute_ut(
    cmd,
    logsdir,
    truncate=False,
    gdb_path='',
    gdb_debug=False,
    valgrind_path='',
    test_mode=False,
    wine_path='',
    stop_signal=None,
):
    outfile = os.path.join(logsdir, "ut.out")
    errfile = os.path.join(logsdir, "ut.err")
    log_gen = LogGenerator(outfile, errfile, logsdir)

    if os.path.exists(valgrind_path):
        cmd = [
            valgrind_path,
            '--tool=memcheck',
            '--track-origins=yes',
            '--leak-check=full',
            '--error-exitcode={}'.format(VALGRIND_ERROR_RC),
        ] + cmd
    else:
        logger.debug("Valgrind is not available: %s", valgrind_path)

    cwd = None
    stdin = None

    if wine_path:
        if os.environ.get("YA_TEST_SHORTEN_WINE_PATH"):
            cmd, cwd, stdin = shorten_wine_paths(wine_path, cmd)
        else:
            cmd = [wine_path] + cmd

    with open(outfile, "ab") as stdout:
        stdout.seek(0, os.SEEK_END)

        logger.debug("Command: '%s' in '%s'", " ".join(cmd), os.getcwd())
        logger.debug("Environment: %s", json.dumps(dict(os.environ), indent=4, sort_keys=True))

        if gdb_debug:
            proc = shared.run_under_gdb(cmd, gdb_path, '/dev/null' if test_mode else '/dev/tty')
        else:
            global SHUTDOWN_REQUESTED
            buff = ""
            buff_size = 64 * 1024
            close_fds = []
            close_files = []
            join_threads = []

            # Each platform provides own implementation of read_stderr_chunk
            if exts.windows.on_win():
                proc = subprocess.Popen(
                    cmd,
                    stdout=stdout,
                    stderr=subprocess.PIPE,
                    **({'text': True, 'encoding': 'utf-8', 'errors': 'ignore'} if six.PY3 else {})
                )

                def reader(proc, buffer):
                    while proc.poll() is None:
                        buffer.append(proc.stderr.read(buff_size))

                chunkid = [0]
                thread_buffer = []
                thread = threading.Thread(target=reader, args=(proc, thread_buffer))
                thread.daemon = True
                thread.start()
                join_threads.append(thread)

                def read_stderr_chunk():
                    cid = chunkid[0]
                    if len(thread_buffer) > cid:
                        data = thread_buffer[cid]
                        # don't store stderr in memory
                        thread_buffer[cid] = ''
                        chunkid[0] = cid + 1
                        return data

            else:
                rfd, wfd = huge_pipe()
                close_fds = [wfd]

                proc = subprocess.Popen(cmd, stdout=stdout, stderr=wfd, stdin=stdin, cwd=cwd)

                # use non-block read on *nix to prevent hanging for process
                # which is going to be smoothly shutdown
                import fcntl

                flag = fcntl.fcntl(rfd, fcntl.F_GETFL)
                fcntl.fcntl(rfd, fcntl.F_SETFL, flag | os.O_NONBLOCK)

                proc_stderr = os.fdopen(rfd, **({'errors': 'ignore'} if six.PY3 else {}))  # XXX until py3
                close_files.append(proc_stderr)

                def read_stderr_chunk():
                    try:
                        return proc_stderr.read(buff_size)
                    except IOError as e:
                        # nothing to read
                        if e.errno != errno.EAGAIN:
                            raise

            while True:
                chunk = read_stderr_chunk()

                if chunk:
                    buff += chunk
                    lines = buff.split('\n')
                    # if buffer doesn't contain incomplete lines
                    # last item will be empty string and we will drop it and reset buffer
                    # otherwise, incomplete line won't be processed, but stored in buffer
                    buff = lines.pop()

                    for line in lines:
                        line = line.rstrip("\r\n")
                        if not line:
                            continue
                        line += '\n'
                        if not line.startswith(START_MARKER) and not line.startswith(FINISH_MARKER):
                            # write stderr immediately for pushing urgency messages
                            # and displaying actual binary stderr for debug purposes (--test-stderr mode for 'ya make').
                            # skip markers
                            sys.stderr.write(line)
                        log_gen.saturate(line)
                else:
                    # there are no data to read - going to wait a little
                    if proc.poll() is None:
                        time.sleep(0.1)
                    # process has finished
                    else:
                        # there is nothing to read from test's stderr
                        # however, there might be incomplete line, which should be processed.
                        # all (start/finish) markers are written with newline symbol,
                        # so we don't need to check is it marker or not
                        if buff:
                            log_gen.saturate(buff)
                        break

                if SHUTDOWN_REQUESTED:
                    stop_signal = stop_signal or signal.SIGQUIT
                    logger.debug("Shutdown requested - sending %s sig to the %d process", stop_signal, proc.pid)
                    # send SIGQUIT or SIGUSR2 to test binary which will dump coverage and call abort() to generate a core dump file,
                    # which will be processed as usual crash case and attached to the test case entry
                    os.kill(proc.pid, stop_signal)
                    break

            for fd in close_fds:
                os.close(fd)
            for afile in close_files:
                afile.close()
            for thread in join_threads:
                thread.join()

        logger.debug("Waiting for process to terminate")
        proc.wait()
        end_time = time.time()
        logger.debug("Return code: %d", proc.returncode)

    logs = log_gen.get_logs()
    if truncate:
        files = [filename for test_data in logs.values() for filename in test_data.values()]
        files = list(filter(os.path.isfile, files))
        logger.debug("Truncating logs to %d bytes", MAX_FILE_SIZE)
        tools.truncate_logs([outfile, errfile] + files, MAX_FILE_SIZE)

    return Result(
        proc.returncode,
        proc.pid,
        logs,
        log_gen.get_last_test_name(),
        log_gen.is_first_test_started(),
        end_time,
        errfile,
        outfile,
    )


def merge_results(suite, performed_suite):
    # replace 'not_launched' tests on actual tests results
    tests = collections.OrderedDict()
    for t in suite.tests:
        tests[t.name] = t
    for t in performed_suite.tests:
        tests[t.name] = t
    suite.chunk.tests = list(tests.values())

    for errors in performed_suite.chunk._errors:
        suite.chunk._errors.append(errors)

    suite.chunk.logs = performed_suite.chunk.logs


def test_case_diff(tc1, tc2):
    return tc1.status != tc2.status or set(tc1.logs.keys()) != set(tc2.logs.keys()) or tc1.comment != tc2.comment


def update_trace_file(tracefile, suite, performed_suite):
    # get suite with changed tests to dump to the tracefile only difference
    diff_suite = gen_suite(suite.project_path)

    test_map = {t.name: t for t in suite.tests}
    for result_test_case in performed_suite.tests:
        test_case = test_map[result_test_case.name]
        if result_test_case.name != test_case.name:
            continue
        if test_case_diff(result_test_case, test_case):
            # append copy of the test_case not ref from performed_suite, which state can be changed in future
            diff_suite.chunk.tests.append(copy.deepcopy(result_test_case))

    for errors in performed_suite.chunk._errors:
        diff_suite.chunk._errors.append(errors)

    diff_suite.logs = performed_suite.logs
    diff_suite.chunk.logs = performed_suite.chunk.logs
    diff_suite.chunk.metrics = performed_suite.chunk.metrics

    # This lock prevents multiple threads from writing to the same common tracefile with status updates simultaneously
    # Otherwise it will lead to races and non-determined test statuses.
    with LOCK:
        shared.dump_trace_file(diff_suite, tracefile)
        merge_results(suite, diff_suite)


def launch_tests(
    suite,
    binary,
    test_names,
    tracefile,
    logsdir,
    truncate,
    collect_cores,
    gdb_path,
    gdb_debug,
    valgrind_path,
    test_mode,
    test_params,
    wine_path,
    stop_signal,
    test_binary_args,
    is_parallel,
    temp_tracefile_dir,
):

    logger.debug("Single launch for %d tests", len(test_names))
    if is_parallel:
        temp_tracefile = os.path.join(temp_tracefile_dir, uuid.uuid4().hex)

    cmd = [binary]

    if is_parallel:
        cmd += ["--trace-path-append", temp_tracefile]
    else:
        cmd += ["--trace-path-append", tracefile]

    for param in test_params:
        cmd += ["--test-param", param]
    cmd += ["+%s" % x for x in test_names]
    for additional_arg in test_binary_args:
        cmd.append(additional_arg)
    res = execute_ut(cmd, logsdir, truncate, gdb_path, gdb_debug, valgrind_path, test_mode, wine_path, stop_signal)

    performed_suite = gen_suite(suite.project_path)

    current_tracefile = temp_tracefile if is_parallel else tracefile

    performed_suite.load_run_results(current_tracefile)

    shared.adjust_test_status(performed_suite, res.rc, res.end_time, add_error_func=suite.add_chunk_error)

    # We need to add subtest-started event to common tracefile from temporary one
    # to correctly display test status in case of timeout.
    # subtest-finished is added further, in update_trace_file.
    if is_parallel:
        with open(current_tracefile, 'r') as trace:
            for line in trace:
                try:
                    json_line = json.loads(line)
                    if json_line.get("name") != "subtest-started":
                        continue
                except json.decoder.JSONDecodeError:
                    continue

                with LOCK:
                    with open(tracefile, 'a') as tr_out:
                        tr_out.write('\n' + line + '\n')

    # Dump intermediate status in case postprocessing takes too much time and wrapper get killed (out of smooth shutdown timeout)
    update_trace_file(tracefile, suite, post_process_suite(performed_suite, res.logs, res.rc))

    if res.rc < 0 and not exts.windows.on_win():
        if res.last_test_name:
            entry_name = res.last_test_name
            logger.debug("Trying to recover dump core file for '%s' test", entry_name)
        else:
            entry_name = "chunk"
            logger.debug("Trying to recover dump core file")

        filename = "{}.{}".format(
            os.path.basename(binary), entry_name.replace("::", ".").replace('/', '.').replace('\\', '.')
        )
        backtrace = shared.postprocess_coredump(
            binary, os.getcwd(), res.pid, res.logs[entry_name], gdb_path, collect_cores, filename, logsdir
        )

        # Fail chunk if test binary has run all tests and was terminated by signal
        if not res.last_test_name:
            msg = '[[bad]]Test was terminated by signal [[imp]]{}[[rst]]'.format(-res.rc)
            if backtrace:
                msg += '\n{}'.format(cores.colorize_backtrace(cores.get_problem_stack(backtrace), TEST_BT_COLORS))

            global SHUTDOWN_REQUESTED
            if SHUTDOWN_REQUESTED and -res.rc == signal.SIGQUIT:
                performed_suite.add_chunk_error(msg, Status.TIMEOUT)
            else:
                performed_suite.add_chunk_error(msg)

    if res.rc != 0:
        stderr_tail = shared.get_tiny_tail(res.stderr_file)
        if not res.first_test_started:
            comment = "[[bad]]Test crashed before first test being executed.[[rst]]"
            if stderr_tail:
                comment += "[[bad]] Test's stderr tail:[[rst]]\n{}".format(stderr_tail)
            performed_suite.add_chunk_error(comment)
        # All tests passed, but suite failed at finalization
        elif performed_suite.get_status() == Status.GOOD:
            comment = "[[bad]]Test has failed with [[imp]]{}[[bad]] exit code.".format(res.rc)
            if stderr_tail:
                comment += "[[bad]] Test's stderr tail:[[rst]]\n{}".format(stderr_tail)
            performed_suite.add_chunk_error(comment)

        if is_darwin():
            logger.debug("Skip symbolizer processing - it does not work properly on mac")
        elif wine_path:
            logger.debug("Skip symbolizer processing - wine dumps backtrace on its own")
        else:
            run_symbolizer(performed_suite, binary)
    if res.rc == VALGRIND_ERROR_RC:
        check_valgrind_errors(performed_suite)
    # Dump fully packed snippet with patched logs
    update_trace_file(tracefile, suite, post_process_suite(performed_suite, res.logs, res.rc))
    return res.rc


def run_symbolizer(suite, binary):
    symbolizer_path = os.environ.get("ASAN_SYMBOLIZER_PATH", None)
    if symbolizer_path and os.path.exists(symbolizer_path):
        addr_re = r"\((0x[0-9a-fxA-FX]+)\)$"
        traces_text = "\n".join([test_case.comment for test_case in suite.tests] + [m for _, m in suite.chunk._errors])
        addrs = re.findall(addr_re, traces_text, re.MULTILINE)

        lines_by_addr = cores.resolve_addresses(addrs, symbolizer_path, binary)

        for addr in lines_by_addr:
            parts = lines_by_addr[addr].split("\n")
            func = "[[alt1]]" + parts[0] + "[[rst]]"
            path = parts[1]
            lines_by_addr[addr] = func + " at " + path

        def resolve_addresses(data):
            comment = ""
            for comment_line in data.splitlines():
                addr = re.findall(addr_re, comment_line)
                if addr and addr[0] in lines_by_addr:
                    comment += lines_by_addr[addr[0]] + "\n"
                else:
                    comment += comment_line + "\n"
            return cores.colorize_backtrace(comment, TEST_BT_COLORS)

        for test_case in suite.tests:
            test_case.comment = resolve_addresses(test_case.comment)

        for i, (t, msg) in enumerate(suite.chunk._errors):
            suite.chunk._errors[i] = (t, resolve_addresses(msg))


def check_valgrind_errors(suite):
    logger.debug(
        "Test failed with valgrind error code %d going to mark last test as crashed (tests count: %d)",
        VALGRIND_ERROR_RC,
        len(suite.tests),
    )
    if suite.tests:
        suite.tests[-1].status = Status.CRASHED


def find_test(suite, test_name):
    for test_case in suite.tests:
        if test_case.name == test_name:
            return test_case


def post_process_suite(suite, logs, rc):
    for test_case in suite.tests:
        # Test was launched and got actual logs to be set
        if test_case.name in logs:
            test_case.logs.update(logs[test_case.name])

            san_error = None
            valgrind_error = None

            if "stderr" in test_case.logs:
                if rc == VALGRIND_ERROR_RC:
                    valgrind_error = shared.get_valgrind_error_summary(test_case.logs["stderr"])
                    if valgrind_error:
                        logger.debug(
                            "Valgrind found errors in test {} - changed state from {} to CRASHED".format(
                                test_case.name, Status.TO_STR[test_case.status]
                            )
                        )
                        test_case.status = Status.CRASHED

                san_error = shared.get_sanitizer_first_error(test_case.logs["stderr"])
                if san_error:
                    logger.debug(
                        "Sanitizer found errors in test {} - changed state from {} to CRASHED".format(
                            test_case.name, Status.TO_STR[test_case.status]
                        )
                    )
                    test_case.status = Status.CRASHED

            if test_case.status == Status.CRASHED:
                error_msg = san_error or valgrind_error or 'See logs for more info'
                test_case.comment = "[[bad]]Test crashed (return code: [[imp]]{}[[bad]])\n{}".format(
                    rc, shared.colorize_sanitize_errors(error_msg)
                )

                if 'backtrace' in test_case.logs:
                    with open(test_case.logs['backtrace']) as afile:
                        backtrace = cores.get_problem_stack(afile.read())
                        backtrace = cores.colorize_backtrace(backtrace, TEST_BT_COLORS)
                        test_case.comment += "\n[[rst]]Problem thread backtrace:\n{}[[rst]]".format(backtrace)
    if "chunk" in logs:
        suite.chunk.logs = logs['chunk']
    return suite


def get_missing_tests(suite, test_names):
    missing = []
    for test_name in test_names:
        test = None
        for test_case in suite.tests:
            if test_case.name == test_name:
                test = test_case
                break
        if not test or test.status == Status.BY_NAME['not_launched']:
            missing.append(test_name)
    return missing


def fill_suite_with_empty_test_records(suite, test_names):
    dups = get_duplicates(test_names)
    for test_name in set(test_names):
        if test_name in dups:
            comment = "Test name duplication found in within one binary unittest ('ut --list-verbose'). Tests are not distinguishable to the test system."
            test_case = facility.TestCase(test_name, Status.FAIL, comment)
        else:
            test_case = facility.TestCase(test_name, Status.NOT_LAUNCHED, "Test was not launched")
        suite.chunk.tests.append(test_case)


def list_tests(test_names):
    for test_case in sorted(test_names):
        print(test_case)


def gen_suite(project_path):
    assert project_path, "Project path must be specified"
    suite = PerformedTestSuite(None, project_path)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def setup_env():
    if hasattr(signal, "SIGUSR2"):
        signal.signal(signal.SIGUSR2, smooth_shutdown)


def main():
    args = parse_args()
    setup_logging(args.verbose)
    setup_env()

    if args.trace_path:
        with open(args.trace_path, "w"):
            pass

    if args.with_wine:
        wine_path = args.wine_path
    else:
        wine_path = None

    suite = gen_suite(args.project_path)

    if args.test_list_path and os.path.exists(args.test_list_path):
        with open(args.test_list_path, 'r') as afile:
            test_names = json.load(afile)[args.modulo_index]
    else:
        try:
            test_classes = get_test_classes(
                args.project_path,
                args.binary,
                args.test_filter,
                args.trace_path,
                args.list_timeout,
                args.gdb_path,
                wine_path,
            )
        except Exception as e:
            # stack trace it not really necessary for listing
            if args.test_list:
                sys.stderr.write(str(e))
                return 1
            # If it's not test listing - tracefile contains all the information about problem
            # We don't need to fail wrapper
            return
        # run only suites (or tests) with specified modulo-index
        test_classes = test_splitter.filter_tests_by_modulo(
            test_classes, args.modulo, args.modulo_index, args.split_by_tests, args.partition_mode
        )
        test_names = [name for data in test_classes.values() for name in data]

    # fill suite with empty test records, where every test got 'not_launched' status.
    # as run_ut runs tests, test results in the suite will be replaced on actual and dumped to tracefile
    # thus, the trace file will be complete, even if run_ut is terminated.
    fill_suite_with_empty_test_records(suite, test_names)
    if not args.test_list:
        dups = get_duplicates(test_names)

        # remove all duplicate tests from a list of tests to run - they are already
        # marked as FAIL in fill_suite_with_empty_test_records method
        test_names = sorted(list(set(test_names) - dups))
        logger.debug("Tests to be launched: %s", test_names)
    else:
        logger.debug("Tests to be launched: %s", sorted(list(set(test_names) - get_duplicates(test_names))))

    if args.test_list:
        list_tests(test_names)
        return

    logsdir = args.output_dir or os.path.dirname(args.trace_path) or os.getcwd()
    suite.chunk.logs = {"logsdir": logsdir}
    shared.dump_trace_file(suite, args.trace_path)

    def launch(tests, logs_directory=logsdir, is_parallel=False, temp_tracefile_dir=None):
        logger.debug("Launching tests %s in logsdir %s", tests, logs_directory)
        res = launch_tests(
            suite,
            args.binary,
            tests,
            args.trace_path,
            logs_directory,
            args.truncate_logs,
            args.collect_cores,
            args.gdb_path,
            args.gdb_debug,
            args.valgrind_path,
            args.test_mode,
            args.test_params,
            wine_path,
            args.stop_signal,
            args.test_binary_args,
            is_parallel,
            temp_tracefile_dir,
        )
        logger.debug("Finished executing tests: %s", tests)
        return res

    try:
        global SHUTDOWN_REQUESTED
        if args.sequential_launch:
            for test_name in test_names:
                launch([test_name])
                if SHUTDOWN_REQUESTED:
                    break
        else:
            while test_names:
                if not args.parallel_tests_within_node_workers:
                    rc = launch(test_names)
                    if SHUTDOWN_REQUESTED or rc in [0, TIMEOUT_RC]:
                        break
                else:
                    workers_count = multiprocessing.cpu_count()
                    if args.parallel_tests_within_node_workers != 'all':
                        workers_count = min(workers_count, int(args.parallel_tests_within_node_workers))

                    if args.cpu_per_test_requested != 0:
                        workers_count = max(workers_count // args.cpu_per_test_requested, 1)

                    logger.debug("Launching tests in parallel with %d workers", workers_count)
                    executor = futures.ThreadPoolExecutor(max_workers=workers_count)
                    runs = []

                    tests_subchunks = [[test_name] for test_name in test_names]

                    if args.temp_tracefile_dir:
                        os.makedirs(args.temp_tracefile_dir, exist_ok=True)

                    for index, subchunk in enumerate(tests_subchunks):
                        logs_directory = os.path.join(logsdir, str(index))
                        os.makedirs(logs_directory, exist_ok=True)

                        runs.append(executor.submit(launch, subchunk, logs_directory, True, args.temp_tracefile_dir))

                    futures.wait(runs, return_when=futures.ALL_COMPLETED)
                    if SHUTDOWN_REQUESTED or all(r.result() in [0, TIMEOUT_RC] for r in runs):
                        break

                # if case of crash there may be missing tests (which were not launched)
                missing = get_missing_tests(suite, test_names)
                logger.debug("Missing tests: %s", missing)
                if len(missing) == len(test_names):
                    suite.add_chunk_error('[[bad]]Test binary failed before test case execution', Status.CRASHED)
                    break
                # keep test named in sorted order
                test_names = sorted(missing)
    except Exception:
        comment = "[[bad]]Internal error: {}".format(traceback.format_exc())
        suite.add_chunk_error(comment, Status.INTERNAL)
        shared.dump_trace_file(suite, args.trace_path)
        raise


if __name__ == "__main__":
    exit(main())
