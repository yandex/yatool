import argparse
from concurrent import futures
import json
import logging
import os
import shutil
import sys
import tempfile
import time

from yatest_lib.test_splitter import get_shuffled_chunk

from devtools.ya.test import const
from devtools.ya.test.system import process

logger = logging.getLogger(__name__)


def setup_logging():
    level = logging.DEBUG
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker-count", type=int, required=True)
    parser.add_argument("--temp-tracefile-dir", required=True)
    return parser.parse_known_args()


def replace_arg(args, key, new_value):
    result = list(args)
    for i, arg in enumerate(result):
        if arg == key and i + 1 < len(result):
            result[i + 1] = new_value
            return result
    return result


def get_arg_value(args, key):
    for i, arg in enumerate(args):
        if arg == key and i + 1 < len(args):
            return args[i + 1]
    return None


def get_test_list(cmd, temp_dir):
    os.makedirs(temp_dir, exist_ok=True)
    list_output = os.path.join(temp_dir, "junit_tests_list.txt")
    list_trace = os.path.join(temp_dir, "listing_trace.json")
    list_cmd = replace_arg(cmd, '--output', list_output)
    list_cmd = replace_arg(list_cmd, '--trace-file', list_trace)
    list_cmd.append('--list')
    process.execute(list_cmd, cwd=temp_dir, check_exit_code=True)
    with open(list_output) as f:
        content = f.read()
    tests = []
    for line in content.strip().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            tests.append(json.loads(line))
        except (json.JSONDecodeError, ValueError):
            parts = line.rsplit('::', 1)
            if len(parts) == 2:
                tests.append({'test': parts[0], 'subtest': parts[1]})
    return tests


def split_into_subchunks(tests, worker_count):
    for i in range(worker_count):
        yield get_shuffled_chunk(tests, worker_count, i, is_sorted=True)


def get_tracefile_path(temp_tracefile_dir, subchunk_number):
    return os.path.join(temp_tracefile_dir, "{}.{}".format(const.TRACE_FILE_NAME, subchunk_number))


def report_subchunk_error(tracefile, message):
    event = {
        "timestamp": time.time(),
        "name": "chunk_event",
        "value": {"errors": [("fail", message)]},
    }
    tracefile.write(json.dumps(event) + "\n")
    tracefile.flush()


def merge_subchunk_traces(target, source_file):
    success = False
    if source_file and os.path.exists(source_file):
        initial_position = target.tell()
        with open(source_file, "r") as source:
            shutil.copyfileobj(source, target)
        target.flush()
        success = (target.tell() - initial_position) > 0
    if not success:
        report_subchunk_error(target, "subchunk finished with empty trace file '{}'".format(source_file))


def execute_subchunk(cmd, tests, subchunk_number, temp_tracefile_dir):
    assert tests

    tracefile_path = get_tracefile_path(temp_tracefile_dir, subchunk_number)

    subchunk_cmd = replace_arg(cmd, '--output', tracefile_path)
    subchunk_cmd = replace_arg(subchunk_cmd, '--trace-file', tracefile_path)

    test_outputs_root = get_arg_value(cmd, '--test-outputs-root')
    if test_outputs_root:
        subchunk_outputs_root = os.path.join(test_outputs_root, 'parallel', str(subchunk_number))
        os.makedirs(subchunk_outputs_root, exist_ok=True)
        subchunk_cmd = replace_arg(subchunk_cmd, '--test-outputs-root', subchunk_outputs_root)

        runner_log_path = get_arg_value(cmd, '--runner-log-path')
        if runner_log_path and runner_log_path.startswith(test_outputs_root):
            subchunk_runner_log = os.path.join(
                subchunk_outputs_root,
                os.path.relpath(runner_log_path, test_outputs_root),
            )
            subchunk_cmd = replace_arg(subchunk_cmd, '--runner-log-path', subchunk_runner_log)

    for test in tests:
        subchunk_cmd.extend(['--filter', '{}::{}'.format(test['test'], test.get('subtest', ''))])

    result = process.execute(subchunk_cmd, check_exit_code=False, wait=False)
    logger.debug("Started subchunk %d: pid=%d", subchunk_number, result.process.pid)
    exit_code = result.process.wait()
    logger.debug("Finished subchunk %d: pid=%d, exit_code=%d", subchunk_number, result.process.pid, exit_code)

    return exit_code, tracefile_path


def main():
    setup_logging()

    options, java_test_cmd = parse_args()

    main_output = get_arg_value(java_test_cmd, '--output')
    if not main_output:
        logger.error("Could not find --output arg in Java test command")
        return 1

    with tempfile.TemporaryDirectory() as temp_dir:
        tests = get_test_list(java_test_cmd, temp_dir)

    if not tests:
        logger.debug("No tests to run")
        return 0

    if options.temp_tracefile_dir:
        os.makedirs(options.temp_tracefile_dir, exist_ok=True)

    worker_count = min(options.worker_count, len(tests))
    logger.debug("Running %d tests with %d workers", len(tests), worker_count)

    subchunks = list(split_into_subchunks(tests, worker_count))

    exit_code = 0
    with futures.ThreadPoolExecutor(max_workers=worker_count) as pool:
        pendings = [
            pool.submit(execute_subchunk, java_test_cmd, chunk_tests, i, options.temp_tracefile_dir)
            for i, chunk_tests in enumerate(subchunks)
            if chunk_tests
        ]
        with open(main_output, 'a') as ya_trace:
            for future in futures.as_completed(pendings):
                try:
                    code, trace_filepath = future.result()
                    merge_subchunk_traces(ya_trace, trace_filepath)
                    if code and not exit_code:
                        exit_code = code
                except Exception as e:
                    logger.exception("Error while running subchunk: %s", e)
                    report_subchunk_error(ya_trace, str(e))
                    exit_code = 1

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
