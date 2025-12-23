import argparse
import json
import logging
import os
import signal
import sys
import six

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
from build.plugins.lib.nots.package_manager.base.utils import (
    build_vs_store_path,
)
from build.plugins.lib.nots.package_manager.pnpm.constants import PNPM_LOCKFILE_FILENAME, VIRTUAL_STORE_DIRNAME


logger = logging.getLogger(__name__)


TEST_STATUS = {
    "passed": const.Status.GOOD,
    "failed": const.Status.FAIL,
    "pending": const.Status.SKIPPED,
    "todo": const.Status.SKIPPED,
}
COVERAGE_INFO_FILENAME = "coverage.jsonl"


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--source-root")
    parser.add_argument("--build-root")
    parser.add_argument("--project-path", help="Relative path to test module directory (my/project/tests)")
    parser.add_argument("--test-work-dir", default=".")
    parser.add_argument("--output-dir")
    parser.add_argument("--test-data-dirs", nargs="*", required=False)
    parser.add_argument("--test-data-dirs-rename", required=False)
    parser.add_argument("--test-for-path", help="Absolute path to testing module ($B/my/project)")
    parser.add_argument(
        "--node-path", help="Absolute path to node_modules in testing module ($B/my/project/node_modules)"
    )
    parser.add_argument("--tracefile")
    parser.add_argument("--config", help="Relative path to jest config, from testing module root")
    parser.add_argument("--timeout", default=0, type=int)
    parser.add_argument("--nodejs")
    parser.add_argument("--ts-config-path", dest="ts_config_path", help="tsconfig.json path", required=True)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--ts-coverage-path", default="")

    args = parser.parse_args()

    return args


def run_tests(opts):
    jest_stdout = os.path.join(opts.output_dir, "jest.out")
    jest_stderr = os.path.join(opts.output_dir, "jest.err")
    jest_results_file = os.path.join(opts.output_dir, "jest_results.json")

    cmd = get_test_cmd(opts, jest_results_file)

    logger.debug("cmd: %s", json.dumps(cmd, indent=4))

    def shutdown(proc):
        if hasattr(signal, "SIGQUIT"):
            proc.send_signal(signal.SIGQUIT)
            proc.wait()

    if getattr(opts, "test_data_dirs"):
        ts_utils.link_test_data(
            opts.build_root, opts.source_root, opts.test_for_path, opts.test_data_dirs, opts.test_data_dirs_rename
        )

    os.chdir(opts.test_for_path)

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

    bindir_node_modules_path = os.path.join(opts.build_root, test_for_project_path, NODE_MODULES_DIRNAME)
    os.environ["NODE_PATH"] = os.pathsep.join(
        [
            os.path.join(build_vs_store_path(opts.build_root, test_for_project_path), NODE_MODULES_DIRNAME),
            os.path.join(bindir_node_modules_path, VIRTUAL_STORE_DIRNAME, NODE_MODULES_DIRNAME),
            bindir_node_modules_path,
        ]
    )

    try:
        res = shared.tee_execute(cmd, jest_stdout, jest_stderr, strip_ansi_codes=False, on_timeout=shutdown)
        exit_code = res.returncode
    except process.SignalInterruptionError:
        exit_code = const.TestRunExitCode.TimeOut

    with open(jest_results_file, "r") as results_stream:
        suite = parse_jest_stdout(opts, results_stream)
        shared.dump_trace_file(suite, opts.tracefile)

    return exit_code


def get_test_cmd(opts, jest_results_file):
    cmd = [
        os.path.join(opts.nodejs, "node"),
        os.path.join(opts.node_path, "jest", "bin", "jest"),
        "--config",
        os.path.join(opts.test_for_path, opts.config),
        "--reporters=default",
        "--json",
        "--outputFile",
        jest_results_file,
        "--ci",
        "--color",
    ]

    cov_path = os.environ.get(const.COVERAGE_TS_ENV_NAME)
    if cov_path:
        cmd += [
            "--coverage",
            "--coverageDirectory",
            # coverage output directory for jest
            cov_path,
            "--coverageReporters",
            # coverage output type ("json" produces single file "coverage-final.json")
            "json",
        ]
    return cmd


def parse_jest_stdout(opts, results_stream):
    results = json.load(results_stream)
    suite = PerformedTestSuite(None, opts.project_path)
    suite.set_work_dir(opts.test_work_dir)
    suite.register_chunk()

    for suite_result in results.get("testResults", []):
        rel_test_path = os.path.relpath(suite_result["name"], opts.test_for_path)
        for test_result in suite_result["assertionResults"]:
            full_test_name = six.ensure_str(
                "{}: {}::{}".format(
                    rel_test_path,
                    " / ".join(test_result["ancestorTitles"]),
                    test_result["title"],
                )
            )
            status = TEST_STATUS[test_result["status"]]
            comment = ""
            if status == const.Status.FAIL:
                comment = "\n\n".join(test_result["failureMessages"])
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
