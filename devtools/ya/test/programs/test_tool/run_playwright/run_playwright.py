import argparse
import logging
import os
import signal

from devtools.ya.test import const
from devtools.ya.test.system import process
from devtools.ya.test.util import shared
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test import facility
from build.plugins.lib.nots.test_utils import ts_utils
from build.plugins.lib.nots.package_manager.base.constants import (
    BUILD_DIRNAME,
    BUNDLE_DIRNAME,
    NODE_MODULES_DIRNAME,
    NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
    PACKAGE_JSON_FILENAME,
)
from build.plugins.lib.nots.package_manager.pnpm.constants import PNPM_LOCKFILE_FILENAME

from .process_json_report import process_json_report


logger = logging.getLogger("run_playwright")


def log_execute_env(env: dict[str, str], cwd: str):
    logger.debug(f"{cwd=}")
    for key, value in sorted(env.items()):
        logger.debug(f"\t{key}={value}")


TEST_STATUS = {
    "expected": const.Status.GOOD,
    "unexpected": const.Status.FAIL,
    "skipped": const.Status.SKIPPED,
    "flaky": const.Status.GOOD,
}


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--source-root")
    parser.add_argument("--build-root")
    parser.add_argument("--project-path", help="Relative path to test module directory (my/project/tests)")
    parser.add_argument("--test-work-dir", default=".")
    parser.add_argument("--output-dir")
    parser.add_argument("--test-for-path", help="Absolute path to testing module ($B/my/project)")
    parser.add_argument(
        "--node-path", help="Absolute path to node_modules in testing module ($B/my/project/node_modules)"
    )
    parser.add_argument("--tracefile")
    parser.add_argument("--config", help="Relative path to playwright config, from testing module root")
    parser.add_argument("--nodejs")
    parser.add_argument("--ts-config-path", dest="ts_config_path", help="tsconfig.json path", required=True)

    args = parser.parse_args()

    return args


def run_tests(opts):
    test_for_project_path = os.path.relpath(
        opts.test_for_path,
        opts.build_root,
    )

    src_path = os.path.join(opts.source_root, test_for_project_path)
    dst_path = opts.test_for_path

    ts_utils.copy_dir_contents(
        src_path,
        dst_path,
        ignore_list=[
            BUILD_DIRNAME,
            BUNDLE_DIRNAME,
            NODE_MODULES_DIRNAME,
            NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            PACKAGE_JSON_FILENAME,
            PNPM_LOCKFILE_FILENAME,
            opts.ts_config_path,
        ],
    )

    ts_utils.create_bin_tsconfig(
        module_arc_path=test_for_project_path,
        source_root=opts.source_root,
        bin_root=opts.build_root,
        ts_config_path=opts.ts_config_path,
    )

    exit_code = 0

    try:
        stdout_file = os.path.join(opts.output_dir, "playwright-test-stdout.log")
        stderr_file = os.path.join(opts.output_dir, "playwright-test-stderr.log")
        report_file = os.path.join(opts.output_dir, "report.json")

        env = os.environ.copy()
        env["PLAYWRIGHT_JSON_OUTPUT_NAME"] = report_file
        env["PATH"] = os.pathsep.join([opts.nodejs, env.get("PATH")])

        cmd = get_test_cmd(opts)

        log_execute_env(
            env=env,
            cwd=opts.test_for_path,
        )

        process.execute(
            cmd,
            check_exit_code=False,
            env=env,
            cwd=opts.test_for_path,
            stdout=stdout_file,
            stderr=stderr_file,
            wait=True,
        )

        with open(report_file, "r", encoding="utf-8") as report:
            suite = create_suite_from_json_report(opts, report.read())
            shared.dump_trace_file(suite, opts.tracefile)

    except process.SignalInterruptionError:
        exit_code = const.TestRunExitCode.TimeOut

    return exit_code


def get_test_cmd(opts):
    cmd = [
        os.path.join(opts.nodejs, "node"),
        os.path.join(opts.node_path, "@playwright", "test", "cli.js"),
        "test",
        "--config",
        os.path.join(opts.test_for_path, opts.config),
        "--reporter=json",
    ]

    return cmd


def create_suite_from_json_report(opts, json_report: str):
    suite = PerformedTestSuite(None, opts.project_path)
    suite.set_work_dir(opts.test_work_dir)
    suite.register_chunk()

    tests, project_dirs = process_json_report(json_report)
    project_dirs = {id: os.path.relpath(dir, opts.test_for_path) for id, dir in project_dirs.items()}

    for test in tests:
        logger.debug(f"{test=}")
        test_status = TEST_STATUS[test.status]
        test_file = os.path.join(project_dirs[test.project_id], test.file)
        title = " / ".join(test.suites + [test.name])

        full_test_name = f"{test_file}[{test.project_name}]::{title}"

        test_case = facility.TestCase(
            name=full_test_name,
            status=test_status,
            comment=test.comment or "",
            elapsed=test.duration / 1000,
            metrics={"tries": test.tries},
        )
        logger.debug(f"{test_case=}")
        suite.chunk.tests.append(test_case)

    return suite


def on_timeout(_signum, _frame):
    raise process.SignalInterruptionError()


def main():
    args = parse_args()
    log_path = os.path.join(args.output_dir, "run_playwright.log")
    shared.setup_logging(logging.DEBUG, log_path)

    if hasattr(signal, "SIGUSR2"):
        signal.signal(signal.SIGUSR2, on_timeout)

    return run_tests(args)


if __name__ == "__main__":
    exit(main())
