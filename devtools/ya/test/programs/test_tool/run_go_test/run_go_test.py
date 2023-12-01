# coding: utf-8

import argparse
import logging
import io
import os
import re
import sys
import time
import signal
import json
import six
import shlex

from yalibrary import display

from test import const
from test.system import process
from test.common import get_test_log_file_path, strings_to_utf8
from devtools.ya.test import facility
from test.test_types.common import PerformedTestSuite
from test.util import shared
from yatest_lib import test_splitter
from exts import fs
import exts.windows
import test.filter as test_filter


logger = logging.getLogger(__name__)

MASTER_TEST_SUITE = "Master Test Suite"

TEST_STATUS = {'PASS': const.Status.GOOD, 'FAIL': const.Status.FAIL, 'SKIP': const.Status.SKIPPED}

LEVEL_INDENT_SIZE = 4

TOTAL_TEST_NAME = '[total]'

DIR_WITH_CANONDATA = 'canon'


def on_timeout(signum, frame):
    raise process.SignalInterruptionError()


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--binary", required=True, help="Path to the unittest binary")
    parser.add_argument("-t", "--trace-path", help="Path to the output trace log")
    parser.add_argument("-o", "--output-dir", help="Path to the output dir")
    parser.add_argument(
        "-f", "--test-filter", default=[], action="append", help="Run only specified tests (binary name or mask)"
    )
    parser.add_argument("-p", "--project-path", help="Project path relative to arcadia")
    parser.add_argument("--timeout", default=0, type=int)
    parser.add_argument("--benchmark-timeout", default=0, type=int)
    parser.add_argument("--modulo", default=1, type=int)
    parser.add_argument("--modulo-index", default=0, type=int)
    parser.add_argument("--partition-mode", default='SEQUENTIAL', help="Split tests according to partitoin mode")
    parser.add_argument("--test-work-dir", help="Path to test work dir", default=".")
    parser.add_argument("--tracefile", help="Path to the output trace log")
    parser.add_argument("--list-tracefile", help="Path to the output list trace log")
    parser.add_argument("--need-list-trace", action="store_true", help="enable trace file mode when listing")
    parser.add_argument("--test-list", action="store_true", help="List of tests")
    parser.add_argument("--bench-run", action="store_true", help="run only benchmarks")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--test-mode", action="store_true")
    parser.add_argument("--gdb-debug", action="store_true")
    parser.add_argument("--gdb-path", help="Path to gdb")
    parser.add_argument("--dlv-debug", action="store_true")
    parser.add_argument(
        "--dlv-args", help="Dlv extra command line options. Has no effect unless --dlv-debug is also specified"
    )
    parser.add_argument("--dlv-path", help="Path to dlv")
    parser.add_argument("--report-deselected", help="Report deselected tests to trace file", action="store_true")
    parser.add_argument("--wine-path", action="store", default="")
    parser.add_argument("--test-param", default=[], action="append", help="Arbitrary parameters to be passed to tests")
    parser.add_argument("--no-subtest-report", action="store_true")
    parser.add_argument("--test-list-path", help="path to test list calculated in list_node", default=None)
    parser.add_argument("--total-report", action="store_true")
    parser.add_argument(
        "--test-binary-args", default=[], action="append", help="Transfer additional parameters to test binary"
    )

    args = parser.parse_args()
    args.binary = os.path.abspath(args.binary)
    if not os.path.exists(args.binary):
        parser.error("Test binary doesn't exist: %s" % args.binary)
    if not args.test_list and not args.tracefile:
        parser.error("Path to the trace file must be specified")
    if args.gdb_debug and args.dlv_debug:
        parser.error("--gdb-debug and --dlv-debug are mutually exclusive")
    if args.dlv_args:
        args.dlv_args = shlex.split(args.dlv_args)
    return args


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def get_default_suite_name(binary):
    return os.path.splitext(os.path.basename(binary))[0]


def get_full_test_name(test_name, subtest_name):
    return six.u("{}::{}").format(test_name, subtest_name)


def get_tests(args, wine_path, stderr=None):
    test_unit_prefix = "Benchmark" if args.bench_run else "Test"
    deselected = []
    if args.test_list_path and os.path.exists(args.test_list_path):
        with open(args.test_list_path, 'r') as afile:
            return json.load(afile)[args.modulo_index], deselected
    else:
        if args.total_report:
            test_list_output = TOTAL_TEST_NAME
        else:
            cmd = [wine_path] if wine_path else []
            cmd += [args.binary, "-test.list", "."]
            res = process.execute(cmd, stderr=stderr, env=os.environ)
            test_list_output = six.ensure_str(res.std_out)
        tests = []
        suite_name = get_default_suite_name(args.binary)
        for test_name in test_list_output.split('\n'):
            if args.total_report or len(test_name) > 0 and test_name.startswith(test_unit_prefix):
                tests.append(get_full_test_name(suite_name, test_name))
        if args.test_filter:
            filter_func = test_filter.make_testname_filter(args.test_filter)
            selected = []
            deselected = []
            for test in tests:
                if filter_func(test):
                    selected.append(test)
                else:
                    deselected.append(test)
        else:
            selected = tests
        if args.modulo != 1:
            chunk_selected = test_splitter.get_splitted_tests(
                selected, args.modulo, args.modulo_index, args.partition_mode
            )
            chunk_deselected = test_splitter.get_splitted_tests(
                deselected, args.modulo, args.modulo_index, args.partition_mode
            )
            return chunk_selected, chunk_deselected
        else:
            return selected, deselected


def gen_suite(project_path):
    suite = PerformedTestSuite(None, project_path)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def list_tests(opts):
    suite_name = get_default_suite_name(opts.binary)
    list_suite = gen_suite(opts.project_path)

    try:
        fs.create_dirs(list_suite.output_dir())
    except OSError:
        logger.debug("output dir aleready exists")

    with open(list_suite.list_stderr_path(), 'w') as std_err:
        tests, _ = get_tests(opts, opts.wine_path, std_err)

    fill_suite(list_suite, suite_name, tests, opts)
    list_suite.chunk.logs['list_stderr'] = list_suite.list_stderr_path()
    shared.dump_trace_file(list_suite, opts.list_tracefile)


def unindent(line, level):
    index = 0
    if len(line) > 0 and level > 0:
        length = min(level * LEVEL_INDENT_SIZE, len(line))
        while index < length and line[index] == ' ':
            index += 1
    return line[index:]


# === RUN   TestA
# === PAUSE TestA
# === RUN   TestAbs
# --- PASS: TestAbs (0.00s)
# === RUN   TestSum
# --- FAIL: TestSum (0.00s)
# === RUN   TestSumAbs
# --- PASS: TestSumAbs (0.00s)
# === RUN   TestHandler
# === RUN   TestHandler//location/locate
# === RUN   TestHandler//location/info?debug=true
# --- PASS: TestHandler (0.00s)
#    --- PASS: TestHandler//location/locate (0.00s)
#    --- PASS: TestHandler//location/info?debug=true (0.00s)
# === RUN   TestSkip
# --- SKIP: TestSkip (0.00s)
#     main_test:go:11:
# === CONT  TestA
# --- PASS: TestA (1.00s):w
# PASS
test_run = re.compile(r'^(.*?)\s*=== (RUN|PAUSE|CONT|NAME)\s+(.+)\s*$')
test_res = re.compile(r'^(.*?)\s*--- (PASS|FAIL|SKIP|BENCH):\s+(.+)\s+\((\d+(\.\d*))s\)\s*$')
suite_res = re.compile('^(PASS|FAIL)$')
log_output = re.compile(r'\s*(Test.*?):\s*(.*?\.go):([0-9]*):\s*(.*)')
# Name            Cpu       Iterations           ns/op                 B/op            allocs/op
# BenchmarkSumAbs-32    	1000000000	         0.368 ns/op	       0 B/op	       0 allocs/op
benchmark_name_regexp = re.compile(r'^\s*Benchmark\S+$')
benchmark_line_regexp = re.compile(r'^\s*(Benchmark\S*?)-(\d+)\s+(\d+)(.*)$')


class TestInfo:
    def __init__(self, status='RUN', duration=0.0, output=[], seq_number=0, metrics=None):
        self.status = status
        self.duration = duration
        self.output = [] + output
        self.seq_number = seq_number
        self.metrics = metrics


class TestStartedError(Exception):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return 'Trying to run the test [{}] which has been already started...'.format(self.name)


class TestNotStartedError(Exception):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return 'Trying to update the status for the test [{}] which has not been been started yet...'.format(self.name)


class TestResultsStore:
    def __init__(self, tests=None, result=None):
        self.tests = tests or {}
        self.result = result
        self.output = ''
        self.parse_error = None

    def add_test(self, name, seq_number=None):
        if name in self.tests:
            raise TestStartedError(name)
        else:
            self.tests[name] = TestInfo(seq_number=seq_number)

    def validate_test_started(self, name):
        if name not in self.tests:
            raise TestNotStartedError(name)

    def update_test_status(self, name, status, duration=None, metrics=None):
        self.validate_test_started(name)
        info = self.tests[name]
        info.status = status
        info.metrics = metrics
        if duration is not None:
            info.duration = float(duration)

    def update_test_output(self, name, output):
        self.validate_test_started(name)
        self.tests[name].output.append(output)

    def fix_subbenchmarks_statuses(self):
        """
        using only for benchmarks.
        """
        empty_test_names = []
        for t in self.tests.keys():
            if self.tests[t].status != "PASS":
                empty_test_names.append(t)
        empty_test_names = reversed(sorted(empty_test_names))
        for empty_test_name in empty_test_names:
            subtests_count = 0
            for test_name, test_info in six.iteritems(self.tests):
                if test_name.startswith(empty_test_name + "/"):
                    subtests_count += 1
                    if test_info.status != "PASS":
                        break
            else:
                if subtests_count != 0:
                    self.tests[empty_test_name].status = "PASS"

    def has_tests(self):
        return len(self.tests) != 0


def get_line_level(line):
    """
    each line in test output has prefix with spaces
    the more spaces in the prefix, the greater the nesting level of the test
    """
    space_count = 0
    for char in line:
        if char == ' ':
            space_count += 1
        else:
            break
    return space_count / LEVEL_INDENT_SIZE


class NotFinishedTest(object):
    def __init__(self, name, seq_number):
        self.name = name
        self.seq_number = seq_number


def parse_suite_results(content):
    test_count = 0
    test_results = TestResultsStore()
    curr_test = None

    try:
        for line in content.split('\n'):
            if len(line) == 0:
                continue
            match = test_run.match(line)
            if match:
                if len(match.group(1)) > 0:
                    test_results.update_test_output(curr_test, match.group(1))
                curr_test = match.group(3)
                if match.group(2) == 'RUN':
                    test_count += 1
                    test_results.add_test(curr_test, test_count)
                else:
                    test_results.update_test_status(curr_test, match.group(2))
            else:
                match = test_res.match(line)
                if match:
                    if len(match.group(1)) > 0:
                        test_results.update_test_output(curr_test, match.group(1))
                    curr_test = match.group(3)
                    test_results.update_test_status(curr_test, match.group(2), duration=match.group(4))
                else:
                    match = suite_res.match(line)
                    if match:
                        assert test_results.result is None
                        test_results.result = const.Status.GOOD if match.group(0) == 'PASS' else const.Status.FAIL
                        curr_test = None
            if not match:
                match = log_output.match(line)
                if match:
                    test_name = match.group(1)
                    log_content = match.group(4)
                    if test_results.has_tests():
                        test_results.update_test_output(test_name, log_content)
                    else:
                        test_results.output += log_content
                elif curr_test:
                    test_results.update_test_output(curr_test, line)
    except (TestStartedError, TestNotStartedError) as e:
        test_results.parse_error = str(e)

    if test_results.result is None:
        test_results.result = const.Status.FAIL
    return test_results


def get_total_test_results(exit_code, elapsed_time):
    test_results = TestResultsStore()
    test_results.add_test(TOTAL_TEST_NAME, 1)
    test_results.update_test_status(TOTAL_TEST_NAME, 'PASS' if exit_code == 0 else 'FAIL', elapsed_time)
    test_results.result = const.Status.GOOD if exit_code == 0 else const.Status.FAIL
    return test_results


def fill_suite(suite, suite_name, test_names, opts):
    for test in test_names:
        splited_test = test.rsplit('::')[-1]
        full_test_name = get_full_test_name(suite_name, splited_test)
        test_case = facility.TestCase(
            full_test_name, const.Status.NOT_LAUNCHED, "test was not launched", path=opts.project_path
        )
        suite.chunk.tests.append(test_case)


def get_not_launched_suite(suite_name, test_names, opts):
    suite = gen_suite(opts.project_path)
    fill_suite(suite, suite_name, test_names, opts)
    return suite


def parse_benchmark_results(content):
    test_results = TestResultsStore()
    suite_info = ''
    test_count = 0
    for line in content.split('\n'):
        if len(line) == 0:
            continue
        match = benchmark_name_regexp.search(line)
        if match:
            test_count += 1
            test_results.add_test(line.strip(), test_count)
        else:
            match = benchmark_line_regexp.search(line)
            if match:
                suite_info += "\n" + line
                test_name = match.group(1)
                cpu_count = int(match.group(2))
                iterations_count = int(match.group(3))

                metrics = {
                    "cpu_count": cpu_count,
                    "iterations_count": iterations_count,
                }

                metrics_tail = match.group(4).strip().split()
                assert len(metrics_tail) % 2 == 0, "every metric must have a name"
                metrics_tail = iter(metrics_tail)

                for value, name in zip(metrics_tail, metrics_tail):
                    name = name.replace("/", "_per_")
                    metrics[name] = value

                ns_per_op = float(metrics.get("ns_per_op", 0))
                test_results.update_test_status(
                    test_name, status="PASS", duration=float(iterations_count * ns_per_op) / 10**9, metrics=metrics
                )

    test_results.fix_subbenchmarks_statuses()

    if test_results.result is None:
        test_results.result = const.Status.FAIL
    return test_results, suite_info


def obtain_backtrace(text):
    prefix = "SIGQUIT: quit\n"
    start = text.find(prefix)
    if start != -1:
        return text[start + len(prefix) :].strip()


def strip_registers(bt):
    m = re.search(r"^r[a-z0-9]+\s+0x[0-9a-f]+$", bt, flags=re.MULTILINE)
    if m:
        bt = bt[: m.start(0)]
    return bt.strip()


def colorize_bt(text):
    filters = [
        # Function names
        (re.compile(r"^([a-z].*)(\(.*\))$", flags=re.MULTILINE), r"[[c:cyan]]\1[[rst]]\2"),
        # Vars
        (re.compile(r"([a-zA-Z]+)="), r"[[c:green]]\1[[rst]]="),
        # Goroutine colorization
        (re.compile(r"^(goroutine [0-9]+ \[.*?\])", flags=re.MULTILINE), r"[[c:light-cyan]]\1[[rst]]"),
        # File path and line number
        (
            re.compile(r"(/[/A-Za-z0-9\+_\.\-]+):([0-9]+)((\s+\+0x[a-f0-9]+)?)$", flags=re.MULTILINE),
            r"[[rst]]\1:[[c:magenta]]\2[[rst]]\3",
        ),
        # Addresses
        (re.compile(r"\b(0x[a-f0-9]+)\b"), r"[[c:light-grey]]\1[[rst]]"),
    ]

    for regex, substitution in filters:
        text = regex.sub(substitution, text)
    return text


def terminate(proc):
    if not exts.windows.on_win():
        proc.send_signal(signal.SIGQUIT)
        proc.wait()
        return proc.returncode


def run_tests(opts):
    if opts.tracefile:
        open(opts.tracefile, "w").close()

    binary = opts.binary
    suite = gen_suite(opts.project_path)
    cmd = [opts.wine_path] if opts.wine_path else []
    cmd += [binary, "-test.v"]

    cov_path = os.environ.get(const.COVERAGE_GO_ENV_NAME)
    if cov_path:
        cov_path = cov_path.format(pid=os.getpid(), time=time.time())
        cmd += ["-test.coverprofile={}".format(cov_path)]

    #  here we get only Test names without benchmarks, subbenchmarks and subtests. thus we can't detect not launched subtests
    tests, deselected = get_tests(opts, opts.wine_path)
    if opts.report_deselected:
        for deselected_test in deselected:
            test_case = facility.TestCase(
                deselected_test, const.Status.DESELECTED, path=opts.project_path, logs={'logsdir': opts.output_dir}
            )
            suite.chunk.tests.append(test_case)
    if not tests:
        shared.dump_trace_file(suite, opts.tracefile)
        return
    suite_name = get_default_suite_name(opts.binary)
    empty_suite = get_not_launched_suite(suite_name, tests, opts)
    shared.dump_trace_file(empty_suite, opts.tracefile)
    run_type = "bench" if opts.bench_run else "run"
    if len(opts.test_filter) > 0 or opts.modulo > 1 or opts.bench_run:
        # cmd += ['-test.run', '^(' + '|'.join(opts.test_filter) + ')$']
        if len(tests) == 0:
            return
        cmd += ['-test.{}'.format(run_type), '^(' + '|'.join([x.rsplit('::', 1)[-1] for x in tests]) + ')$']
        if opts.bench_run:
            if opts.benchmark_timeout:
                cmd += ['-test.benchtime', "{}s".format(opts.benchmark_timeout)]
            cmd += ['-test.benchmem']
            cmd += ['-test.run', "$^"]

    if opts.test_param:
        logger.warning("Parameters passed in --test-param is available in 'TestParam' library function")
        logger.warning("You can pass additional arguments to test binary using --test-binary-args option")
        cmd.extend([x for x in opts.test_param])
    else:
        cmd.extend([x for x in opts.test_binary_args])

    std_out, std_err = '', ''
    test_elapsed_time = 0
    if opts.gdb_debug:
        proc = shared.run_under_gdb(cmd, opts.gdb_path, None if opts.test_mode else '/dev/tty')
        proc.wait()
        exit_code = proc.returncode
    elif opts.dlv_debug:
        proc = shared.run_under_dlv(cmd, opts.dlv_path, None if opts.test_mode else '/dev/tty', opts.dlv_args or [])
        proc.wait()
        exit_code = proc.returncode
    else:
        start_time = time.time()
        go_stderr = os.path.join(opts.output_dir, 'go.err')
        go_stdout = os.path.join(opts.output_dir, 'go.out')
        try:
            logger.debug("cmd: %s", cmd)
            res = shared.tee_execute(cmd, go_stdout, go_stderr, strip_ansi_codes=False, on_timeout=terminate)
            exit_code = res.returncode
        except process.SignalInterruptionError:
            exit_code = const.TestRunExitCode.TimeOut
        test_elapsed_time = time.time() - start_time

        with io.open(go_stdout, 'r', errors='ignore', encoding='utf-8') as stdout:
            std_out = stdout.read()
        with io.open(go_stderr, 'r', errors='ignore', encoding='utf-8') as stderr:
            std_err = stderr.read()
    canon_test_to_result = {}
    canon_dir = os.path.join(opts.test_work_dir, DIR_WITH_CANONDATA)
    if os.path.exists(canon_dir):
        canon_files = set(os.listdir(canon_dir))
        for cf in canon_files:
            with open(os.path.join(canon_dir, cf), 'r') as afile:
                canon_result = json.load(afile)
                canon_test_to_result[canon_result['test_name']] = canon_result['data']

    passed_test_elapsed = 0
    results = TestResultsStore()

    if len(std_out) > 0:
        if opts.bench_run:
            results, suite_info = parse_benchmark_results(std_out)
            suite.add_chunk_info(suite_info)
        else:
            if opts.total_report:
                results = get_total_test_results(exit_code, test_elapsed_time)
            else:
                results = parse_suite_results(std_out)
        first_not_finished = None
        for test_name, info in six.iteritems(results.tests):
            if opts.no_subtest_report and '/' in test_name:
                continue
            full_test_name = get_full_test_name(suite_name, test_name)

            if info.status in ["RUN", "CONT", "PAUSE", "NAME"] and exit_code == const.TestRunExitCode.TimeOut:
                if '/' in test_name:  # it's subtest and it wasn't added to empty suite before
                    test_case = facility.TestCase(
                        full_test_name,
                        const.Status.NOT_LAUNCHED,
                        "Subtest was launched as a part of a test function that exceeded timeout",
                        path=opts.project_path,
                    )
                    empty_suite.chunk.tests.append(test_case)

                if (first_not_finished is None) or info.seq_number < first_not_finished.seq_number:
                    first_not_finished = NotFinishedTest(test_name, info.seq_number)
                continue
            status = TEST_STATUS.get(info.status, results.result)
            passed_test_elapsed += info.duration
            snippet = '\n'.join(strings_to_utf8(info.output))

            test_log_path = get_test_log_file_path(opts.output_dir, full_test_name)
            with open(test_log_path, 'w') as log:
                log.write(display.strip_markup(snippet))

            test_case = facility.TestCase(
                full_test_name,
                status,
                snippet,
                elapsed=info.duration,
                path=opts.project_path,
                logs={'log': test_log_path, 'logsdir': opts.output_dir},
                result=canon_test_to_result.get(test_name, None),
                metrics=info.metrics,
            )
            suite.chunk.tests.append(test_case)
        if first_not_finished is not None:
            test_elapsed_time -= passed_test_elapsed
            full_test_name = get_full_test_name(suite_name, first_not_finished.name)
            info = results.tests[first_not_finished.name]
            test_log_path = get_test_log_file_path(opts.output_dir, full_test_name)
            test_case = facility.TestCase(
                full_test_name,
                const.Status.CRASHED,
                info.output,
                elapsed=test_elapsed_time,
                path=opts.project_path,
                logs={'log': test_log_path, 'logsdir': opts.output_dir},
                result=canon_test_to_result.get(first_not_finished.name, None),
                metrics=info.metrics,
            )
            suite.chunk.tests.append(test_case)
    shared.dump_trace_file(empty_suite, opts.tracefile)

    if exit_code not in [0, const.TestRunExitCode.TimeOut]:
        if (
            cov_path
            and "cannot use -test.coverprofile because test binary was not built with coverage enabled" in std_err
        ):
            suite.add_chunk_error('Test did not provide coverage data', const.Status.DESELECTED)
        elif results and results.result == const.Status.GOOD:
            suite.add_chunk_error(
                '[[bad]]Test crashed with exit code: {} but all tests passed[[rst]]'.format(exit_code)
            )
        elif results is None or exit_code != 1:
            suite.add_chunk_error(
                '[[bad]]Test crashed with exit code: {}[[rst]]'.format(exit_code), const.Status.CRASHED
            )
    elif exit_code == const.TestRunExitCode.TimeOut:
        bt = obtain_backtrace(std_err)
        if bt:
            bt_filename = os.path.join(opts.output_dir, "backtrace.txt")
            with open(bt_filename, "w") as afile:
                afile.write(bt)
            suite.chunk.logs['backtrace'] = bt_filename

            bt = strip_registers(bt)
            bt = colorize_bt(bt)
            suite.add_chunk_error(
                '[[bad]]Chunk exceeded timeout - dumping goroutine stacktraces\n{}[[rst]]'.format(bt),
                const.Status.TIMEOUT,
            )
        else:
            logger.debug("No backtrack found among stderr log")
    elif results.parse_error:
        suite.add_chunk_error(
            '[[bad]]Test output parser error: {}[[rst]]'.format(results.parse_error), const.Status.CRASHED
        )

    shared.dump_trace_file(suite, opts.tracefile)


def main():
    args = parse_args()
    setup_logging(args.verbose)

    if hasattr(signal, "SIGUSR2"):
        signal.signal(signal.SIGUSR2, on_timeout)

    if args.test_list:
        if args.need_list_trace:
            list_tests(args)
        else:
            tests, _ = get_tests(args, args.wine_path)
            sys.stderr.write("\n".join(tests))
        return 0

    run_tests(args)
    return 0


if __name__ == "__main__":
    exit(main())
