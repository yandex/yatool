import argparse
from concurrent import futures
import json
import logging
import os
import shutil
import sys
import tempfile
import time
from typing import Any

from yatest_lib.test_splitter import get_shuffled_chunk

from devtools.ya.test import const
from devtools.ya.test.system import process

logger = logging.getLogger(__name__)

LIST_ARGUMENTS = ["--collect-only", "--mode", "list", "-qqq"]

FILTER_ARGUMENTS = [
    "--modulo",
    "--modulo-index",
    "--partition-mode",
    "--split-by-tests",
    "--test-filter",
    "--test-file-filter",
]


def setup_logging():
    level = logging.DEBUG
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--worker-count", type=int, help="number of execution threads")

    parser.add_argument("--binary")
    parser.add_argument("--basetemp")
    parser.add_argument("--ya-trace")
    parser.add_argument("--build-root")
    parser.add_argument("--output-dir")
    parser.add_argument("--temp-tracefile-dir")

    for arg in FILTER_ARGUMENTS:
        parser.add_argument(arg)

    return parser.parse_known_args()


def get_run_cmd(
    options: argparse.Namespace,
    binary_args: list[str],
    tracefile: str | None = None,
    temp_dir: str | None = None,
    output_dir: str | None = None,
    filter_file: str | None = None,
) -> list[str]:
    cmd = [
        options.binary,
        "--build-root",
        options.build_root,
        "--basetemp",
        temp_dir or options.basetemp,
        "--ya-trace",
        tracefile or options.ya_trace,
        "--output-dir",
        output_dir or options.output_dir,
    ]
    if not filter_file:
        for arg in FILTER_ARGUMENTS:
            value = getattr(options, arg[2:].replace("-", "_"), None)
            if value:
                cmd.extend((arg, value))
    else:
        cmd.extend(("--test-filter-file", filter_file))
    return cmd + binary_args


def get_temporary_directory(build_root: str, sequence_number: int) -> str:
    return os.path.join(build_root, "tmp", str(sequence_number))


def get_output_directory(output_dir: str, subchunk_number: int) -> str:
    return os.path.join(output_dir, "test_runner", str(subchunk_number))


def get_tracefile_path(temp_tracefile_dir: str, subchunk_number: int) -> str:
    return os.path.join(temp_tracefile_dir, "{}.{}".format(const.TRACE_FILE_NAME, subchunk_number))


def get_test_list(cmd: list[str], temp_dir) -> list[dict[str, Any]]:
    os.makedirs(temp_dir, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w+t', delete=True, dir=temp_dir) as temp_file:
        process.execute(cmd + LIST_ARGUMENTS + ["--test-list-file", temp_file.name], check_exit_code=True)
        return json.loads(temp_file.read())


def report_subchunk_error(tracefile: Any, message: str):
    event = {
        "timestamp": time.time(),
        "name": "chunk_event",
        "value": {"errors": [("fail", message)]},
    }
    tracefile.write(json.dumps(event) + "\n")
    tracefile.flush()


def merge_subchunk_traces(target: Any, source_file: str | None):
    success = False
    if source_file and os.path.exists(source_file):
        initial_position = target.tell()
        with open(source_file, "r") as source:
            shutil.copyfileobj(source, target)
        target.flush()
        success = (target.tell() - initial_position) > 0
    if not success:
        report_subchunk_error(target, f"subchunk finished with empty trace file '{source_file}'")


def split_into_subchunks(tests, worker_count: int):
    for i in range(worker_count):
        yield get_shuffled_chunk(tests, worker_count, i, is_sorted=True)


def execute_tests(
    options: argparse.Namespace,
    binary_args: list[str],
    tests: list[dict[str, Any]],
    subchunk_number: int,
):
    assert tests

    temp_dir = get_temporary_directory(options.build_root, subchunk_number)
    tracefile = get_tracefile_path(options.temp_tracefile_dir, subchunk_number)
    output_dir = get_output_directory(options.output_dir, subchunk_number)

    os.makedirs(temp_dir)
    os.makedirs(output_dir)

    filter_file = os.path.join(temp_dir, "test_filter.txt")
    with open(filter_file, 'w') as fd:
        fd.write("\n".join("{}::{}".format(test["class"], test["test"]) for test in tests))

    test_command = get_run_cmd(
        options,
        binary_args,
        tracefile=tracefile,
        temp_dir=temp_dir,
        output_dir=output_dir,
        filter_file=filter_file,
    )
    with (
        open(os.path.join(output_dir, "stdout"), "w") as stdout_file,
        open(os.path.join(output_dir, "stderr"), "w") as stderr_file,
    ):
        result = process.execute(
            test_command,
            stdout=stdout_file,
            stderr=stderr_file,
            check_exit_code=False,
            wait=False,
        )
        logger.debug(
            "Started subchunk %d: pid=%d, command='%s'", subchunk_number, result.process.pid, " ".join(test_command)
        )
        exit_code = result.process.wait()
        logger.debug("Finished subchunk %d: pid=%d, exit_code=%d", subchunk_number, result.process.pid, exit_code)

    return exit_code, tracefile


def main():
    setup_logging()

    options, binary_args = parse_args()

    cmd = get_run_cmd(options, binary_args)

    tests_list = get_test_list(cmd, temp_dir=options.basetemp)
    if not tests_list:
        logger.debug("No tests to run")
        return 0

    if options.temp_tracefile_dir:
        os.makedirs(options.temp_tracefile_dir, exist_ok=True)

    if options.worker_count > len(tests_list):
        logger.debug("Reduce number of workers to count of tests, from %d to %d", options.worker_count, len(tests_list))
        worker_count = len(tests_list)
    else:
        worker_count = options.worker_count

    logger.debug("Running %d tests with %d workers", len(tests_list), options.worker_count)
    subchunks = split_into_subchunks(tests_list, worker_count)

    exit_code = 0
    with futures.ThreadPoolExecutor(max_workers=worker_count) as pool:
        pendings = [pool.submit(execute_tests, options, binary_args, tests, i) for i, tests in enumerate(subchunks)]
        with open(options.ya_trace, "a") as ya_trace:
            for future in futures.as_completed(pendings):
                try:
                    code, trace_filepath = future.result()
                    merge_subchunk_traces(ya_trace, trace_filepath)
                    if code and not exit_code:
                        exit_code = code
                except Exception as e:
                    logger.exception("Error while running test: %s", e)
                    report_subchunk_error(ya_trace, str(e))
                    exit_code = 1
    return exit_code


if __name__ == "__main__":
    exit(main())
