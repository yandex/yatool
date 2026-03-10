import logging
import os
import shutil
import stat

import build.plugins.lib.nots.package_manager.constants as pm_const
import build.plugins.lib.nots.package_manager.utils as pm_utils
from devtools.ya.test.const import Status
from devtools.ya.test.facility import TestCase
from devtools.ya.test.system.process import execute
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test.util.shared import setup_logging

from .cli_args import parse_args, CliArgs

logger = logging.getLogger("run_ts_check")


def main():
    args = parse_args()
    # logging.INFO is a level of stderr stream
    # for args.log_path stream level is logging.DEBUG
    setup_logging(logging.INFO, args.log_path)
    return run(args)


def get_env(args: CliArgs, report_path: str):
    build_dir = os.path.join(args.build_root, args.target_path)
    bindir_node_modules_path = os.path.join(build_dir, pm_const.NODE_MODULES_DIRNAME)
    node_path = [
        bindir_node_modules_path,
        os.path.join(pm_utils.build_vs_store_path(args.build_root, args.target_path), pm_const.NODE_MODULES_DIRNAME),
        # TODO: remove - no longer needed
        os.path.join(bindir_node_modules_path, pm_const.VIRTUAL_STORE_DIRNAME, pm_const.NODE_MODULES_DIRNAME),
    ]

    return {
        "PATH": args.nodejs,
        "NODE_PATH": os.pathsep.join(node_path),
        "NODE_OPTIONS": "--max-old-space-size=4096",
        "YA_TEST_REPORT_PATH": report_path,
    }


def copy_files(src_dir: str, build_dir: str, files: list[str]):
    created_dirs = set()

    def makedirs(path: str):
        if path not in created_dirs:
            os.makedirs(path, exist_ok=True)
            created_dirs.add(path)

    for file in files:
        src_file = os.path.join(src_dir, file)
        dst_file = os.path.join(build_dir, file)
        if not os.path.exists(dst_file):
            makedirs(os.path.dirname(dst_file))
            shutil.copyfile(src_file, dst_file, follow_symlinks=False)
            os.chmod(dst_file, os.stat(dst_file).st_mode | stat.S_IWRITE)


def run(args: CliArgs):
    logger.debug(f"{args.source_root=}")
    logger.debug(f"{args.build_root=}")
    logger.debug(f"{args.target_path=}")

    report_path = os.path.join(args.output_dir, "report.jsonl")
    node_run_out = os.path.join(args.output_dir, "node-run.out")
    node_run_err = os.path.join(args.output_dir, "node-run.err")
    src_dir = os.path.join(args.source_root, args.target_path)
    build_dir = os.path.join(args.build_root, args.target_path)
    cwd = build_dir

    copy_files(src_dir, build_dir, args.files)
    cmd = get_cmd(args)

    res = execute(
        cmd,
        cwd=cwd,
        env=get_env(args, report_path),
        check_exit_code=False,
        stdout=node_run_out,
        stderr=node_run_err,
    )

    suite = PerformedTestSuite(None, None, None)
    suite.set_work_dir(cwd)
    suite.register_chunk()
    suite.chunk.logs[os.path.basename(args.log_path)] = args.log_path

    messages = []
    if res.exit_code != 0:
        messages = [
            " ".join(cmd),
            f"Exit code: {res.exit_code}",
            "stdout:",
            res.stdout,
            "stderr:",
            res.stderr,
        ]

    test_case = TestCase(
        f"{args.test_type}::node-run",
        Status.FAIL if res.exit_code != 0 else Status.GOOD,
        "\n".join(messages),
        logs={os.path.basename(node_run_out): node_run_out, os.path.basename(node_run_err): node_run_err},
    )
    suite.chunk.tests.append(test_case)

    if os.path.exists(report_path):
        test_cases = load_tests(report_path)
        suite.chunk.tests.extend(test_cases)

    logger.debug(f"Generate trace file '{args.tracefile}'")
    logger.debug(f"Found tests count: {len(suite.chunk.tests)}")
    suite.generate_trace_file(args.tracefile)
    return 0


def get_cmd(args: CliArgs):
    cmd = [
        "node",
        "--run",
        args.script_name,
    ]
    return cmd


def load_tests(report_path: str):
    test_cases = []
    # for file_name, entry in result.items():
    #     test_case = TestCase(
    #         "{}::typecheck".format(file_name),
    #         Status.FAIL if entry['has_errors'] else Status.GOOD,
    #         "\n".join(entry['details']),
    #     )
    #     test_cases.append(test_case)
    return test_cases
