import argparse
import logging
import os

import build.plugins.lib.nots.package_manager.base.constants as pm_const
import build.plugins.lib.nots.package_manager.pnpm.constants as pnpm_const
import build.plugins.lib.nots.test_utils.ts_utils as ts_utils
from devtools.ya.test.const import Status
from devtools.ya.test.facility import TestCase
from devtools.ya.test.system.process import execute
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test.util.shared import setup_logging
from .parse_output import parse_output


logger = logging.getLogger(__name__)


def main():
    args = parse_args()
    setup_logging(args.log_level, args.log_path)
    return run(args)


def run(args):
    logger.debug(f"{args.source_root=}")
    logger.debug(f"{args.build_root=}")
    logger.debug(f"{args.source_folder_path=}")

    src_dir = os.path.join(args.source_root, args.source_folder_path)
    build_dir = os.path.join(args.build_root, args.source_folder_path)
    cwd = build_dir

    suite = PerformedTestSuite(None, None, None)
    suite.set_work_dir(cwd)
    suite.register_chunk()

    ts_utils.copy_dir_contents(
        src_dir,
        build_dir,
        ignore_list=[
            pm_const.BUILD_DIRNAME,
            pm_const.BUNDLE_DIRNAME,
            pm_const.NODE_MODULES_DIRNAME,
            pm_const.NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            pm_const.PACKAGE_JSON_FILENAME,
            pnpm_const.PNPM_LOCKFILE_FILENAME,
            args.ts_config_path,
        ],
    )

    ts_utils.create_bin_tsconfig(
        module_arc_path=args.source_folder_path,
        source_root=args.source_root,
        bin_root=args.build_root,
        ts_config_path=args.ts_config_path,
    )

    cmd = get_cmd(args)

    # Apparently suite.set_work_dir is not enough to make this work in the cwd, passing directly
    res = execute(cmd, cwd=cwd, check_exit_code=False)

    if res.exit_code != 0 and len(res.stdout or '') == 0 and len(res.stderr or '') != 0:
        return 1

    fill_tests(src_dir, res.stdout, args.files, suite)
    trace_file = args.tracefile
    logger.debug(f"Generate trace file '{trace_file}'")
    logger.debug(f"Found tests count: {len(suite.chunk.tests)}")
    suite.generate_trace_file(trace_file)
    return 0


def get_cmd(args):
    node_path = os.path.join(args.build_root, args.source_folder_path, "node_modules")
    cmd = [
        os.path.join(args.nodejs, "node"),
        os.path.join(node_path, "typescript", "bin", "tsc"),
        "--project",
        args.ts_config_path,
        "--noEmit",
        "--incremental",
        "false",
        "--composite",
        "false",
        "--pretty",
    ]
    return cmd


def fill_tests(src_dir: str, output: str, files: list[str], suite):
    logger.debug(f"Parse TSC output with src_dir {src_dir}")
    logger.debug(f"Files to check count: {len(files)}")
    src_dir += "/"
    result = {}
    for file_name in files:
        finally_file_name = file_name.replace(src_dir, "")
        result[finally_file_name] = {
            'has_errors': False,
            'details': [],
        }

    parsed_output = parse_output(output)
    logger.debug(f"{parsed_output=}")
    for file_name, messages in parsed_output.items():
        result[file_name] = {
            'has_errors': True,
            'details': messages,
        }

    for file_name, entry in result.items():
        test_case = TestCase(
            "{}::typecheck".format(file_name),
            Status.FAIL if entry['has_errors'] else Status.GOOD,
            "\n".join(entry['details']),
        )
        suite.chunk.tests.append(test_case)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", dest="source_root", help="Source root", required=True)
    parser.add_argument("--build-root", dest="build_root", help="Build root", required=True)
    parser.add_argument("--source-folder-path", dest="source_folder_path", required=True)
    parser.add_argument("--nodejs", dest="nodejs", help="Path to the Node.JS resource", required=True)
    parser.add_argument("--ts-config-path", dest="ts_config_path", help="tsconfig.json path", required=True)
    parser.add_argument("--tracefile", help="Path to the output trace log")
    parser.add_argument("--log-path", dest="log_path", help="Log file path")
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="Logging level",
        action="store",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument("files", nargs='*')
    return parser.parse_args()
