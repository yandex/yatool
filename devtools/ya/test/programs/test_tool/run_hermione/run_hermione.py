import argparse
import json
import logging
import os
import traceback

import devtools.ya.test.const as test_const
import devtools.ya.test.system.process
import devtools.ya.test.test_types.common as test_types
import devtools.ya.test.util.shared as util_shared
import devtools.ya.test.filter
import build.plugins.lib.nots.test_utils.ts_utils as ts_utils

from build.plugins.lib.nots.package_manager.base.constants import (
    BUILD_DIRNAME,
    BUNDLE_DIRNAME,
    NODE_MODULES_DIRNAME,
    NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
    PACKAGE_JSON_FILENAME,
)

from devtools.ya.test import facility

logger = logging.getLogger(__name__)

TEST_STATUS_MAP = {
    "success": test_const.Status.GOOD,
    "fail": test_const.Status.FAIL,
    "retry": test_const.Status.FAIL,
    "skipped": test_const.Status.SKIPPED,
}


def main():
    args = parse_args()
    setup_logging(args)

    logger.info('Used Node.js resource: "{}"'.format(args.nodejs))
    logger.info(repr(args))

    cwd = args.test_for_path
    suite = gen_suite(args.project_path, cwd)

    try:
        prepare_tests_data(args)
        prepare_files(args)

        tests, exit_code = read_tests(args, suite, cwd)

        if exit_code != 0:
            return exit_code

        if len(tests) == 0:
            return 0

        fill_suite_by_not_launched_tests(tests, suite, args.tracefile)

        return run_tests(args, suite, cwd)
    except Exception:
        comment = "[[bad]]Internal error: {}".format(traceback.format_exc())
        add_error(args, suite, comment, test_const.Status.INTERNAL)

        raise


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--source-root", help="Path to source root")
    parser.add_argument("--build-root", help="Path to build root")
    parser.add_argument("--test-work-dir", help="Path to test work dir in build root", default=".")
    parser.add_argument("-o", "--output-dir", help="Path to the output dir in build root")
    parser.add_argument("--tracefile", help="Path to the output trace log in build root")
    parser.add_argument("-p", "--project-path", help="Project path relative to arcadia")
    parser.add_argument("--source-test-dir", help="Path to test sources dir in source root")
    parser.add_argument("--build-test-dir", help="Path to compiled tests inside test dir of build root")
    parser.add_argument(
        "--test-data-dirs",
        default=[],
        nargs="*",
        required=False,
        help="Paths to tests data in source root",
    )
    parser.add_argument(
        "--test-data-dirs-rename",
        help="Rules with paths matching from curdir to bindir. Paths must be separated by colons and rules by semicolons (example: dir1:dir2;dir3:dir4/dir5)",
        required=False,
    )
    parser.add_argument("--test-for-path", help="Path to tested library in build root")
    parser.add_argument("--node-path", help="Path to node_modules inside tested library of build root")
    parser.add_argument(
        "-f",
        "--test-filter",
        default=[],
        action="append",
        help="Run only specified tests (binary name or mask)",
    )
    parser.add_argument("--nodejs", help="Path to the Node.JS resource")
    parser.add_argument("--log-path", dest="log_path", help="Log file path")
    parser.add_argument("--chunks-count", default=1, type=int)
    parser.add_argument("--run-chunk", default=1, type=int)
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="Logging level",
        action="store",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument("files", nargs="*")
    # hermione args
    parser.add_argument("--config", help="Path to hermione config")

    args = parser.parse_args()

    return args


def setup_logging(args):
    if args.log_level:
        util_shared.setup_logging(args.log_level, args.log_path)


def prepare_files(args):
    test_for_project_path = os.path.relpath(
        args.test_for_path,
        args.build_root,
    )

    src_path = os.path.join(args.source_root, test_for_project_path)
    dst_path = args.test_for_path

    ts_utils.copy_dir_contents(
        src_path,
        dst_path,
        ignore_list=[
            BUILD_DIRNAME,
            BUNDLE_DIRNAME,
            NODE_MODULES_DIRNAME,
            NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            PACKAGE_JSON_FILENAME,
        ],
    )


def prepare_tests_data(args):
    ts_utils.link_test_data(
        args.build_root,
        args.source_root,
        args.test_for_path,
        args.test_data_dirs,
        args.test_data_dirs_rename,
    )


def gen_suite(project_path, cwd):
    suite = test_types.PerformedTestSuite(None, project_path, test_const.TestSize.Large)
    suite.set_work_dir(cwd)
    suite.register_chunk()
    return suite


def fill_suite_by_not_launched_tests(tests, suite, tracefile):
    for t in tests:
        test_case = gen_not_launched_test_case(t)
        suite.chunk.tests.append(test_case)

    util_shared.dump_trace_file(suite, tracefile)


def gen_not_launched_test_case(test):
    full_test_name = get_full_test_name(test["file"], test["fullTitle"], test["browserId"])

    return facility.TestCase(
        full_test_name,
        test_const.Status.NOT_LAUNCHED,
        "Test was not launched",
    )


def add_error(args, suite, comment, status=test_const.Status.FAIL):
    suite.add_chunk_error(comment, status)
    util_shared.dump_trace_file(suite, args.tracefile)


def read_tests(args, suite, cwd):
    output_file = os.path.join(args.output_dir, test_const.HERMIONE_TESTS_READ_FILE_NAME)

    cmd = [
        os.path.join(args.nodejs, "node"),
        os.path.join(args.node_path, "@yandex-int", "hermione-cli", "bin", "hermione-cli"),
        "read-tests",
        "--disable-plugins",
        "--output-file",
        output_file,
    ]

    if args.chunks_count > 1:
        cmd += ["--use-chunks"]

    cmd += prepare_cmd_args(args, ctx="read")

    out_file = os.path.join(args.output_dir, test_const.HERMIONE_TESTS_READ_STDOUT_FILE_NAME)
    err_file = os.path.join(args.output_dir, test_const.HERMIONE_TESTS_READ_STDERR_FILE_NAME)

    configure_plugins(args, cwd)

    res = devtools.ya.test.system.process.execute(cmd, check_exit_code=False, stdout=out_file, stderr=err_file, cwd=cwd)

    if res.exit_code != 0:
        if len(res.std_err) > 0:
            comment = "[[bad]]Read hermione tests is failed with exit code: [[imp]]{}[[rst]]\n{}".format(
                res.exit_code, res.std_err
            )
            add_error(args, suite, comment)

        return [], res.exit_code

    with open(output_file) as f:
        tests = json.load(f)

        if args.chunks_count > 1 and len(tests) == 0:
            logger.warning(
                "[[bad]]There are no tests found to run in chunk: [[imp]]{}[[bad]]. Try to decrease the number of chunks.\n".format(
                    args.run_chunk
                )
            )

        return tests, 0


def prepare_cmd_args(args, suite=None, ctx=None):
    cmd_args = []

    if args.config:
        cmd_args += ["--config", args.config]

    if args.test_filter and ctx == "run" and suite:
        cmd_args += convert_test_filter_to_cmd_args(args, suite)
    else:
        cmd_args += args.files

    return cmd_args


def convert_test_filter_to_cmd_args(args, suite):
    files = set()
    titles = set()
    browsers = set()
    cmd_args = []

    filter_fn = devtools.ya.test.filter.make_testname_filter(args.test_filter)

    for test_case in suite.chunk.tests:
        if not filter_fn(test_case.name):
            test_case.status = test_const.Status.DESELECTED
            continue

        file, title, browser = test_case.name.split(test_const.TEST_SUBTEST_SEPARATOR)

        files.add(file)
        titles.add(title)
        browsers.add(browser)

    if not len(files):
        raise Exception('Tests not found by option: "--test-filter {}"'.format(args.test_filter))

    cmd_args += ["--grep", "|".join(titles)]

    for b in browsers:
        cmd_args += ["--browser", b]

    cmd_args += files

    return cmd_args


def configure_plugins(args, cwd, html_report_folder=None):
    # configure hermione-chunks plugin
    if args.chunks_count > 1:
        os.environ["hermione_chunks_enabled"] = "true"
        os.environ["hermione_chunks_count"] = str(args.chunks_count)
        os.environ["hermione_chunks_run"] = str(args.run_chunk)

    # configure html-reporter plugin
    if html_report_folder:
        # use relative path for correct work of gui mode
        os.environ["html_reporter_path"] = os.path.relpath(html_report_folder, cwd)


def get_full_test_name(file_path, full_title, browser_id):
    return "{}{separator}{}{separator}{}".format(
        file_path, full_title, browser_id, separator=test_const.TEST_SUBTEST_SEPARATOR
    )


def run_tests(args, suite, cwd):
    test_results_file = os.path.join(args.output_dir, test_const.HERMIONE_TESTS_RUN_FILE_NAME)
    # create a file for parsing at a runtime
    open(test_results_file, "w").close()

    cmd = [
        os.path.join(args.nodejs, "node"),
        os.path.join(args.node_path, "hermione", "bin", "hermione"),
    ]

    cmd += prepare_cmd_args(args, suite, ctx="run")
    cmd += [
        "--reporter",
        '{{"type": "jsonl", "path": "{}"}}'.format(test_results_file),
        "--reporter",
        "flat",
    ]

    out_file = os.path.join(args.output_dir, test_const.HERMIONE_TESTS_RUN_STDOUT_FILE_NAME)
    err_file = os.path.join(args.output_dir, test_const.HERMIONE_TESTS_RUN_STDERR_FILE_NAME)
    html_report_folder = os.path.join(args.output_dir, test_const.HERMIONE_REPORT_DIR_NAME)

    configure_plugins(args, cwd, html_report_folder)

    logs = {
        "html_report": os.path.join(html_report_folder, test_const.HERMIONE_REPORT_INDEX_FILE_NAME),
    }

    proc = devtools.ya.test.system.process.execute(
        cmd,
        check_exit_code=False,
        stdout=out_file,
        stderr=err_file,
        wait=False,
        cwd=cwd,
    )
    handle_test_results(test_results_file, proc, suite, logs, args.tracefile)
    proc.wait(check_exit_code=False)

    if proc.exit_code != 0:
        comment = "[[bad]]Run hermione tests is failed with exit code: [[imp]]{}".format(proc.exit_code)
        add_error(args, suite, comment)

    return proc.exit_code


def handle_test_results(test_results_file, proc, suite, logs, tracefile):
    with open(test_results_file) as f:
        while True:
            line = f.readline()
            line_length = len(line)

            if line_length == 0:
                if not proc.running:
                    break
                else:
                    continue

            if not line[line_length - 1] == "\n":
                f.seek(f.tell() - line_length)
                continue

            test_result = json.loads(line.strip())
            handle_test_result(test_result, suite, logs)
            # TODO: try to add only specific test or batch of tests to tracefile - https://st.yandex-team.ru/HERMIONE-223
            util_shared.dump_trace_file(suite, tracefile)


def handle_test_result(test_result, suite, logs):
    file = test_result.get("file")
    full_title = test_result.get("fullTitle")
    browser_id = test_result.get("browserId")
    full_test_name = get_full_test_name(file, full_title, browser_id)
    status = TEST_STATUS_MAP.get(test_result.get("status"))
    elapsed = test_result.get("duration", 0) * 0.001
    started = test_result.get("startTime", 0)
    comment = ""

    if status == test_const.Status.FAIL:
        comment = test_result.get("error")
    elif status == test_const.Status.SKIPPED:
        comment = test_result.get("reason")

    test_case = facility.TestCase(
        full_test_name,
        status,
        comment,
        elapsed=elapsed,
        started=started,
        logs=logs,
    )

    suite.chunk.tests.append(test_case)
