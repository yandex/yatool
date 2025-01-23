import argparse
import json
import logging
import os
import signal
import six
import sys

import library.python.archive as archive

from devtools.ya.test import const
from devtools.ya.test.system import process
from devtools.ya.test.util import shared, tools
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test import facility
from build.plugins.lib.nots.test_utils import ts_utils
from build.plugins.lib.nots.typescript import DEFAULT_TS_CONFIG_FILE
from build.plugins.lib.nots.package_manager.base.constants import (
    BUILD_DIRNAME,
    BUNDLE_DIRNAME,
    NODE_MODULES_DIRNAME,
    NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
    PACKAGE_JSON_FILENAME,
)
from build.plugins.lib.nots.package_manager.pnpm.constants import PNPM_LOCKFILE_FILENAME


logger = logging.getLogger(__name__)


TEST_STATUS = {
    "passed": const.Status.GOOD,
    "failed": const.Status.FAIL,
    "pending": const.Status.SKIPPED,
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

    args = parser.parse_args()

    return args


def run_tests(opts):
    cmd = get_test_cmd(opts)

    stdout_file = os.path.join(opts.output_dir, "stdout")
    stderr_file = os.path.join(opts.output_dir, "stderr")

    if getattr(opts, "test_data_dirs"):
        ts_utils.link_test_data(
            opts.build_root, opts.source_root, opts.test_for_path, opts.test_data_dirs, opts.test_data_dirs_rename
        )

    # Unpack to opts.build_root to not spam working directory even more (as it will be brought as results dir)
    env = os.environ.copy()
    browsers_arch_path = os.path.join(opts.test_work_dir, "playwright-browsers.tgz")
    if os.path.exists(browsers_arch_path):
        browsers_directory = os.path.join(opts.build_root, "playwright-browsers")
        required_libs_directory = os.path.join(browsers_directory, "required-ubuntu-x86-64-libs")
        os.makedirs(browsers_directory, exist_ok=True)
        archive.extract_tar(browsers_arch_path, browsers_directory)
        env["LD_LIBRARY_PATH"] = required_libs_directory
        env["PLAYWRIGHT_BROWSERS_PATH"] = browsers_directory

    os.chdir(opts.test_for_path)

    test_for_project_path = os.path.relpath(
        opts.test_for_path,
        opts.build_root,
    )

    src_path = os.path.join(opts.source_root, test_for_project_path)
    dst_path = opts.test_for_path

    tools.copy_dir_contents(
        src_path,
        dst_path,
        ignore_list=[
            BUILD_DIRNAME,
            BUNDLE_DIRNAME,
            NODE_MODULES_DIRNAME,
            NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            PACKAGE_JSON_FILENAME,
            PNPM_LOCKFILE_FILENAME,
            DEFAULT_TS_CONFIG_FILE,
        ],
    )

    ts_utils.create_bin_tsconfig(
        module_arc_path=test_for_project_path,
        source_root=opts.source_root,
        bin_root=opts.build_root,
    )

    results_file = "results.json"
    try:
        env["PLAYWRIGHT_JSON_OUTPUT_NAME"] = results_file
        env["PATH"] = os.pathsep.join([env.get("PATH"), opts.nodejs])
        res = process.execute(
            cmd,
            check_exit_code=False,
            env=env,
            stdout=stdout_file,
            stderr=stderr_file,
            wait=True,
        )

        exit_code = res.returncode
        logger.debug(f"cmd: {cmd}, exited with code: {exit_code}")

        with open(results_file, "r") as stdout:
            suite = parse_stdout(opts, stdout)
            shared.dump_trace_file(suite, opts.tracefile)

    except process.SignalInterruptionError:
        exit_code = const.TestRunExitCode.TimeOut

    return exit_code


def get_test_cmd(opts):
    cmd = [
        os.path.join(opts.nodejs, "node"),
        os.path.join(opts.node_path, "playwright", "cli.js"),
        "test",
        "--config",
        os.path.join(opts.test_for_path, opts.config),
        "--reporter=json",
    ]

    return cmd


def parse_stdout(opts, stdout):
    results = json.load(stdout)
    suite = PerformedTestSuite(None, opts.project_path)
    suite.set_work_dir(opts.test_work_dir)
    suite.register_chunk()

    for suite_result in results.get("suites", []):
        rel_test_path = os.path.join(os.path.relpath(opts.test_work_dir, opts.build_root), suite_result["file"])
        for spec in suite_result["specs"]:
            test_name = spec["title"]
            test_engine = spec["tests"][0]["projectId"]
            test_status = spec["tests"][0]["results"][0]["status"]
            full_test_name = six.ensure_str("{}: {}::{}".format(rel_test_path, test_engine, test_name))
            status = TEST_STATUS[test_status]
            comment = ""
            if status == const.Status.FAIL:
                comment = spec["tests"][0]["results"][0]["error"]["message"]
            suite.chunk.tests.append(facility.TestCase(full_test_name, status, comment=comment, path=opts.project_path))

    return suite


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(
        level=level,
        stream=sys.stdout,
        format="%(asctime)s (%(relativeCreated)d): %(levelname)s: [%(process)d|%(thread)d]: %(message)s",
    )


def on_timeout(_signum, _frame):
    raise process.SignalInterruptionError()


def main():
    args = parse_args()
    setup_logging(args.verbose)

    if hasattr(signal, "SIGUSR2"):
        signal.signal(signal.SIGUSR2, on_timeout)

    return run_tests(args)


if __name__ == "__main__":
    exit(main())
