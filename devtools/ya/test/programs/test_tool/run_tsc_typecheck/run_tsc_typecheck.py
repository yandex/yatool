import argparse
import logging
import os

import build.plugins.lib.nots.package_manager.base.constants as pm_const
import build.plugins.lib.nots.package_manager.pnpm.constants as pnpm_const
import build.plugins.lib.nots.test_utils.ts_utils as nots_ts_utils
import build.plugins.lib.nots.typescript as nots_typescript

import devtools.ya.test
import devtools.ya.test.const
import devtools.ya.test.system.process
import devtools.ya.test.test_types.common
import devtools.ya.test.util

from .parse_output import parse_output

logger = logging.getLogger(__name__)


def main():
    args = parse_args()
    devtools.ya.test.util.shared.setup_logging(args.log_level, args.log_path)
    return run(args)


def run(args):
    logger.debug(f"{args.source_root=}")
    logger.debug(f"{args.build_root=}")
    logger.debug(f"{args.source_folder_path=}")

    src_dir = os.path.join(args.source_root, args.source_folder_path)
    build_dir = os.path.join(args.build_root, args.source_folder_path)
    cwd = build_dir

    suite = devtools.ya.test.test_types.common.PerformedTestSuite(None, None, None)
    suite.set_work_dir(cwd)
    suite.register_chunk()

    devtools.ya.test.util.tools.copy_dir_contents(
        src_dir,
        build_dir,
        ignore_list=[
            pm_const.BUILD_DIRNAME,
            pm_const.BUNDLE_DIRNAME,
            pm_const.NODE_MODULES_DIRNAME,
            pm_const.NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            pm_const.PACKAGE_JSON_FILENAME,
            pnpm_const.PNPM_LOCKFILE_FILENAME,
            nots_typescript.DEFAULT_TS_CONFIG_FILE,
        ],
    )

    nots_ts_utils.create_bin_tsconfig(
        module_arc_path=args.source_folder_path,
        source_root=args.source_root,
        bin_root=args.build_root,
    )

    cmd = get_cmd(args)

    # Apparently suite.set_work_dir is not enough to make this work in the cwd, passing directly
    res = devtools.ya.test.system.process.execute(cmd, cwd=cwd, check_exit_code=False)

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
        result[file_name[len(src_dir) :]] = {
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
        test_case = devtools.ya.test.facility.TestCase(
            "{}::typecheck".format(file_name),
            devtools.ya.test.const.Status.FAIL if entry['has_errors'] else devtools.ya.test.const.Status.GOOD,
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
