import argparse
import json
import logging
import os

import devtools.ya.test.const as test_const
import devtools.ya.test.system.process as process
import devtools.ya.test.test_types.common as test_types
import devtools.ya.test.util.shared as util_shared
import devtools.ya.test.util.tools as tools
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
from devtools.ya.test.test_types.common import PerformedTestSuite

logger = logging.getLogger(__name__)

TEST_STATUS = {
    "passed": test_const.Status.GOOD,
    "failed": test_const.Status.FAIL,
    "pending": test_const.Status.SKIPPED,
    "timedOut": test_const.Status.TIMEOUT,
}


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--source-root")
    parser.add_argument("--build-root")
    parser.add_argument("--project-path", help="Relative path to test module directory (my/project/tests)")
    parser.add_argument("--test-work-dir", default=".")
    parser.add_argument("--output-dir")
    parser.add_argument("--test-data-dirs", nargs="*", required=False)
    parser.add_argument("--test-for-path", help="Absolute path to testing module ($B/my/project)")
    parser.add_argument(
        "--node-path", help="Absolute path to node_modules in testing module ($B/my/project/node_modules)"
    )
    parser.add_argument("--tracefile")
    parser.add_argument("--config", help="Relative path to playwright config, from testing module root")
    parser.add_argument("--timeout", default=0, type=int)
    parser.add_argument("--nodejs")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="Logging level",
        action="store",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument("--log-path", dest="log_path", help="Log file path")

    parser.add_argument("files", nargs="*")

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

    tools.copy_dir_contents(
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


def prepare_cmd_args(args, suite=None, ctx=None):
    cmd_args = []

    if args.config:
        cmd_args += ["--config", args.config]

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


def get_full_test_name(file_path, full_title, browser_id):
    return "{}{separator}{}{separator}{}".format(
        file_path, full_title, browser_id, separator=test_const.TEST_SUBTEST_SEPARATOR
    )


def run_tests(args):
    logger.info(repr(args))

    prepare_files(args)

    cwd = args.test_for_path
    suite = gen_suite(args.project_path, cwd)

    test_results_file = os.path.join(args.output_dir, "test_results.jsonl")
    open(test_results_file, "w+").close()

    cmd = [
        "npx",
        "playwright",
        "test",
        "--reporter=json",
    ]

    out_file = os.path.join(args.output_dir, "run_tests.out")
    err_file = os.path.join(args.output_dir, "run_tests.err")
    results_file = os.path.join(args.output_dir, "results.json")
    # TODO: Recheck where can we show the results to the user
    # zipped_results = os.path.join(args.output_dir, "blob-report.zip")

    logs = {
        "json_report": results_file,
        # "zipped_results": zipped_results,
    }

    env = os.environ.copy()
    env["PLAYWRIGHT_JSON_OUTPUT_NAME"] = results_file
    env["PW_TEST_HTML_REPORT_OPEN"] = "never"
    # env["PLAYWRIGHT_BLOB_OUTPUT_FILE"] = zipped_results

    if "PW_EXECUTE_CMD" in env:
        cmd = env["PW_EXECUTE_CMD"].split(" ") + [out_file, err_file, results_file] + cmd

    try:
        res = process.execute(
            cmd,
            check_exit_code=False,
            env=env,
            stdout=out_file,
            stderr=err_file,
            wait=True,
            cwd=cwd,
        )

        exit_code = res.returncode
        logger.debug(f"cmd: {cmd}, exited with code: {exit_code}")

        with open(results_file, "r") as stdout:
            suite = parse_stdout(args, stdout, logs)
            util_shared.dump_trace_file(suite, args.tracefile)

    except process.SignalInterruptionError:
        exit_code = test_const.TestRunExitCode.TimeOut

    return exit_code


def parse_suite_results(opts, suite_results, suite, logs):
    if "suites" in suite_results and suite_results["suites"]:
        for subsuite_results in suite_results["suites"]:
            parse_suite_results(opts, subsuite_results, suite, logs)
    if "specs" in suite_results and suite_results["specs"]:
        rel_test_path = os.path.join(os.path.relpath(opts.test_work_dir, opts.build_root), suite_results["file"])
        for spec in suite_results["specs"]:
            test_name = spec["title"]
            test_engine = spec["tests"][0]["projectId"]
            test_status = spec["tests"][0]["results"][0]["status"]
            full_test_name = f"{rel_test_path}: {test_engine}::{test_name}"
            status = TEST_STATUS[test_status] if test_status in TEST_STATUS else test_const.Status.FAIL
            comment = ""
            if status == test_const.Status.FAIL:
                if "error" in spec["tests"][0]["results"][0]:
                    comment = spec["tests"][0]["results"][0]["error"]["message"]
            suite.chunk.tests.append(
                facility.TestCase(full_test_name, status, comment=comment, path=opts.project_path, logs=logs)
            )


def parse_stdout(opts, stdout, logs):
    results = json.load(stdout)
    suite = PerformedTestSuite(None, opts.project_path)
    suite.set_work_dir(opts.test_work_dir)
    suite.register_chunk()

    for suite_results in results.get("suites", []):
        parse_suite_results(opts, suite_results, suite, logs)

    return suite


def main():
    args = parse_args()

    logger.info('Used Node.js resource: "{}"'.format(args.nodejs))
    logger.info(repr(args))

    setup_logging(args)

    return run_tests(args)


if __name__ == "__main__":
    exit(main())
