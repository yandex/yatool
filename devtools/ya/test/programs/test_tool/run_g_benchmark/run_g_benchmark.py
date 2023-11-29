import argparse
import json
import logging
import os
import sys
import signal
import six

import exts
import test.filter as test_filter
from test import const
from devtools.ya.test import facility
from devtools.ya.test.programs.test_tool.lib import benchmark
from test.util import shared
from test.system import process

logger = logging.getLogger(__name__)
BENCH_STDOUT = "benchmark.out"
BENCH_STDERR = "benchmark.err"


# Adapted from python 3.7+ re.escape, but escapes only the necessary chars (gbench doesn't accept python's extra escapes)
_special_chars_map = {ord(c): six.ensure_text('\\' + c) for c in '([{?*+|^$\\.'}


def escape_regex(s):
    return six.ensure_str(six.ensure_text(s).translate(_special_chars_map))


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--binary", required=True, help="Path to the unittest binary")
    parser.add_argument("-t", "--tracefile", help="Path to the output trace log")
    parser.add_argument("-o", "--output-dir", help="Path to the output dir")
    parser.add_argument("-f", "--test-filter", default=[], action="append", help="Run only specified tests (binary name or mask)")
    parser.add_argument("-p", "--project-path", help="Project path relative to arcadia")
    parser.add_argument("--gdb-path", help="gdb path")
    parser.add_argument("--gdb-debug", action="store_true")
    parser.add_argument("--test-mode", action="store_true")
    parser.add_argument("--test-list", action="store_true", help="List of tests")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--truncate-logs", action="store_true")
    parser.add_argument("--test-binary-args", default=[], action="append", help="Transfer additional parameters to test binary")

    args = parser.parse_args()
    args.binary = os.path.abspath(args.binary)
    args.need_core = not args.truncate_logs
    if not os.path.exists(args.binary):
        parser.error("Test binary doesn't exist: %s" % args.binary)
    if not args.test_list and not args.tracefile:
        parser.error("Path to the trace file must be specified")
    return args


def dump_test_info(suite_name, binary, tracefile, exit_code, benchmarks_run_output_path, project_path, output_path, res, gdb_path, need_core):
    suite = benchmark.gen_suite(project_path)
    with open(benchmarks_run_output_path, 'r') as afile:
        benchmark_result = json.load(afile)

    benchmark_result_list = benchmark_result.get("benchmarks", [])

    for b in benchmark_result_list:
        if b.get("run_type", None) != "iteration":
            metrics = {}
            for key, value in six.iteritems(b):
                try:
                    metric_value = float(value)
                    metrics[key] = metric_value
                except ValueError:
                    continue
            status = const.Status.GOOD
            elapsed_time = 0
        else:
            metric_keys = {"real_time", "cpu_time", "iterations", "repetitions", "repetition_index"}
            if metric_keys & set(b.keys()) != metric_keys:
                metrics = {}
                status = const.Status.TIMEOUT
                elapsed_time = 0
            else:
                real_time = b["real_time"]
                cpu_time = b["cpu_time"]
                iterations = b["iterations"]
                repetitions = b["repetitions"]
                repetition_index = b["repetition_index"]
                metrics = {
                    "real_time_ns": real_time,
                    "cpu_time_ns": cpu_time,
                    "iterations": iterations,
                    "repetitions": repetitions,
                    "repetition_index": repetition_index,
                }
                status = const.Status.GOOD
                elapsed_time = float(iterations * cpu_time) / (10 ** 9)
        if "name" in b:
            full_test_name = suite_name + "::" + b["name"]
            suite.chunk.tests.append(facility.TestCase(full_test_name, status, path=project_path, metrics=metrics, elapsed=elapsed_time))

    if exit_code:
        if exit_code == const.TestRunExitCode.TimeOut:
            suite.add_chunk_error("[[bad]]benchmark was killed by timeout[[rst]]", status=const.Status.TIMEOUT)
        else:
            suite.add_chunk_error("[[bad]]Test crashed with exit_code: {}[[rst]]".format(exit_code), status=const.Status.CRASHED)

    if res and res.returncode < 0 and not exts.windows.on_win():
        filename = os.path.basename(binary)
        shared.postprocess_coredump(binary, os.getcwd(), res.pid, suite.chunk.logs, gdb_path, need_core, filename, output_path)
    if os.path.exists(BENCH_STDOUT):
        with open(BENCH_STDOUT, 'r') as afile:
            stdout_content = "\n" + afile.read()
            suite.add_chunk_info(stdout_content)
    shared.dump_trace_file(suite, tracefile)


def get_filter(filters):
    return test_filter.make_testname_filter(filters)


def try_fix_output_after_fail(benchmarks_run_output_path):
    with open(benchmarks_run_output_path, 'r') as afile:
        content = afile.read()
    stack = []
    opening_tokens = {"(", "[", "{"}
    closing_tokens = {")", "]", "}"}
    tokens = {"(": ")", "[": "]", "{": "}"}
    for ch in content:
        if ch in opening_tokens:
            stack.append(ch)
        if ch in closing_tokens:
            stack.pop()
    tail = ""
    while stack:
        tail += tokens[stack.pop()]
    content += tail
    with open(benchmarks_run_output_path, 'w') as afile:
        afile.write(content)


def run_benchmarks(binary, benchmarks_list, output_path, benchmarks_run_output_path, additional_arguments=None, gdb_path=None, gdb_debug=False, test_mode=False):
    additional_arguments = additional_arguments or []

    benchmark_filter = '|'.join(["^{}$"] * len(benchmarks_list)).format(*map(escape_regex, benchmarks_list)) or "$^"
    cmd = [
        binary,
        "--benchmark_filter={}".format(benchmark_filter),
        "--benchmark_format=console",
        "--benchmark_out={}".format(benchmarks_run_output_path),
        "--benchmark_out_format=json",
    ]

    for additional_arg in additional_arguments:
        cmd.append(additional_arg)
    logger.info("cmd: %s", " ".join(cmd))

    exit_code = 0
    res = None
    if benchmarks_list:
        if gdb_debug:
            proc = shared.run_under_gdb(cmd, gdb_path, None if test_mode else '/dev/tty')
            proc.wait()
            exit_code = proc.returncode
        else:
            try:
                def shutdown_with_core(r):
                    if hasattr(signal, "SIGQUIT"):
                        os.kill(r.pid, signal.SIGQUIT)
                        r.wait()
                res = shared.tee_execute(cmd, BENCH_STDOUT, BENCH_STDERR, strip_ansi_codes=False, on_timeout=shutdown_with_core)
                exit_code = res.returncode
            except process.SignalInterruptionError as e:
                res = e.res
                exit_code = const.TestRunExitCode.TimeOut
            if exit_code:
                try_fix_output_after_fail(benchmarks_run_output_path)
    else:
        with open(benchmarks_run_output_path, 'w') as afile:
            afile.write("{}")

    if os.path.exists(benchmarks_run_output_path):
        with open(benchmarks_run_output_path, 'r') as afile:
            logger.info("benchmarks_run_output_path content: %s", afile.read())

    return exit_code, res


def list_all(binary_path):
    res = process.execute([binary_path, "--benchmark_list_tests=true"])
    exit_code = res.returncode
    std_out = six.ensure_str(res.std_out)
    std_err = six.ensure_str(res.std_err)
    if exit_code:
        return [], exit_code
    logger.info("list stdout: %s", std_out)
    logger.info("list stderr: %s", std_err)
    return list(std_out.strip().split('\n')), exit_code


def filter_benchmarks(test_filter, suite_name, benchmarks_list):
    benchmarks_filter = get_filter(test_filter)
    benchmarks_list = benchmark.get_full_benchmark_names(suite_name, benchmarks_list)
    logger.info("benchmark_list after concatenating: %s", benchmarks_list)
    benchmarks_list = list(filter(benchmarks_filter, benchmarks_list))
    benchmarks_list = [x.split("::")[1] for x in benchmarks_list]
    if not benchmarks_list:
        logger.warning("Failed to match any benchmarks against filters %s", test_filter)
    return benchmarks_list


def main():
    args = parse_args()
    benchmark.setup_logging(args.verbose)

    if hasattr(signal, "SIGUSR2"):
        signal.signal(signal.SIGUSR2, benchmark.on_timeout)

    benchmarks_list, list_exit_code = list_all(args.binary)
    suite_name = benchmark.get_suite_name(args.binary)
    logger.info("benchmark_list: %s", benchmarks_list)
    if list_exit_code:
        benchmark.dump_fail_listing_suite(args.tracefile, list_exit_code, args.project_path)
        return 0
    if args.test_filter:
        benchmarks_list = filter_benchmarks(args.test_filter, suite_name, benchmarks_list)

    logger.info("benchmark_list after filtering: %s", benchmarks_list)

    if args.test_list:
        sys.stderr.write('\n'.join(benchmark.get_full_benchmark_names(suite_name, benchmarks_list)))
        return 0

    suite_name = benchmark.get_suite_name(args.binary)
    logger.info("suite_name: %s", suite_name)

    benchmark.dump_listed_benchmarks(suite_name, args.tracefile, benchmarks_list, args.project_path)

    benchmarks_run_output_path = os.path.join(args.output_dir, "bench_out.json")
    logger.info("benchmarks_run_output_path: %s", benchmarks_run_output_path)

    exit_code, res = run_benchmarks(args.binary, benchmarks_list, args.output_dir, benchmarks_run_output_path, args.test_binary_args, args.gdb_path, args.gdb_debug, args.test_mode)

    dump_test_info(suite_name, args.binary, args.tracefile, exit_code, benchmarks_run_output_path, args.project_path, args.output_dir, res, args.gdb_path, args.need_core)

    return 0


if __name__ == "__main__":
    exit(main())
