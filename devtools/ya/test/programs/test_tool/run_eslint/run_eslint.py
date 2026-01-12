# coding=utf-8
import argparse
import logging
import os
import re

import devtools.ya.test.system.process
from build.plugins.lib.nots.package_manager.constants import (
    BUILD_DIRNAME,
    BUNDLE_DIRNAME,
    NODE_MODULES_DIRNAME,
    NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
    PACKAGE_JSON_FILENAME,
    PNPM_LOCKFILE_FILENAME,
    VIRTUAL_STORE_DIRNAME,
)
from build.plugins.lib.nots.package_manager.utils import (
    build_vs_store_path,
)
from build.plugins.lib.nots.test_utils import ts_utils

from devtools.ya.test import facility
from devtools.ya.test.const import Status
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test.util import shared

logger = logging.getLogger(__name__)


def main():
    args = parse_args()

    shared.setup_logging(args.log_level, args.log_path)
    logger.debug("source_root: {}\nbuild_root: {}".format(args.source_root, args.build_root))
    logger.debug("source_folder_path: {}".format(args.source_folder_path))

    src_dir = os.path.join(args.source_root, args.source_folder_path)
    build_dir = os.path.join(args.build_root, args.source_folder_path)
    cwd = build_dir

    bindir_node_modules_path = os.path.join(build_dir, NODE_MODULES_DIRNAME)
    node_path = [
        os.path.join(build_vs_store_path(args.build_root, args.source_folder_path), NODE_MODULES_DIRNAME),
        # TODO: remove - no longer needed
        os.path.join(bindir_node_modules_path, VIRTUAL_STORE_DIRNAME, NODE_MODULES_DIRNAME),
        bindir_node_modules_path,
    ]

    suite = PerformedTestSuite(None, None, None)
    suite.set_work_dir(cwd)
    suite.register_chunk()

    ts_utils.copy_dir_contents(
        src_dir,
        build_dir,
        ignore_list=[
            BUILD_DIRNAME,
            BUNDLE_DIRNAME,
            NODE_MODULES_DIRNAME,
            NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            PACKAGE_JSON_FILENAME,
            PNPM_LOCKFILE_FILENAME,
            args.ts_config_path,
        ],
    )

    ts_utils.create_bin_tsconfig(
        module_arc_path=args.source_folder_path,
        source_root=args.source_root,
        bin_root=args.build_root,
        ts_config_path=args.ts_config_path,
    )

    cmd = get_cmd(args, args.files)
    env = {"NODE_PATH": os.pathsep.join(node_path), "TEST_TOOL": "true"}

    # Apparently suite.set_work_dir is not enough to make this work in the cwd, passing directly
    res = devtools.ya.test.system.process.execute(cmd, cwd=cwd, env=env, check_exit_code=False)

    logger.debug("cwd: {}\nenv: {}\ncmd: {}\nexit_code: {}".format(cwd, env, cmd, res.exit_code))
    logger.debug("eslint stdout:\n" + res.std_out or '')
    logger.debug("eslint stderr:\n" + res.std_err or '')

    if res.exit_code < 0:
        logger.error("ESLint was terminated by signal: %s", res.exit_code)
        return 1
    elif res.exit_code > 0 and len(res.std_out or '') == 0 and len(res.std_err or '') != 0:
        logger.error(res.std_err)
        return 1

    clean_stdout = shared.clean_ansi_escape_sequences(res.std_out)

    parse_stylish_output(build_dir, clean_stdout, args.files, suite)
    trace_file = args.tracefile
    logger.debug("Generate trace file '{}'".format(trace_file))
    suite.generate_trace_file(trace_file)
    return 0


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", dest="source_root", help="Source root", required=True)
    parser.add_argument("--build-root", dest="build_root", help="Build root", required=True)
    parser.add_argument("--source-folder-path", dest="source_folder_path", required=True)
    parser.add_argument("--nodejs", dest="nodejs", help="Path to the Node.JS resource", required=True)
    parser.add_argument("--ts-config-path", dest="ts_config_path", help="tsconfig.json path", required=True)
    parser.add_argument("--eslint-config-path", dest="eslint_config_path", help="ESLint config path", required=True)
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


def get_cmd(args, files):
    node_path = os.path.join(args.build_root, args.source_folder_path, "node_modules")
    cmd = [
        os.path.join(args.nodejs, "node"),
        os.path.join(node_path, "eslint", "bin", "eslint.js"),
        "--config",
        args.eslint_config_path,
        "--format=stylish",
        "--color",
    ]
    cmd = cmd + files
    return cmd


def parse_stylish_output(exec_dir, output, files, suite):
    """
        Parse ESLint output `--format=stylish`, add found errors and warnings to the suite.

        :param exec_dir: abs path to the linted module directory; used to match `files` to abs path in the output
        :type exec_dir: str
        :param output: stdout from eslint command
        :type output: str
        :param files: list of all checked files
        :type files: list(str)
        :param suite: test suite
        :type suite: PerformedTestSuite

    Input params example:

        exec_dir: '/home/vturov/.ya/build/build_root/0je1/000006/devtools/dummy_arcadia/typescript/with_lint'
        files: ['src/index.test.ts', 'src/index.ts', 'tests/index.test.ts']
        output:

    /home/vturov/.ya/build/build_root/0je1/000006/devtools/dummy_arcadia/typescript/with_lint/tests/index.test.ts
       1:7   error    'triggerUnusedVarError' is assigned a value but never used     no-unused-vars
      12:14  warning  Identifier 'trigger_camelcase_warning_2' is not in camel case  camelcase

    """

    logger.debug('Parse ESLint output with exec_dir {}\n{}\n'.format(exec_dir, output))
    result = {}
    for file_name in files:
        result[file_name] = {
            'has_errors': False,
            'details': [],
        }

    parser = re.compile(r'^\s+\d+:\d+\s+(error|warning)\s+.+$')
    file_name = None
    has_errors = False
    details = []
    for line in output.split('\n'):
        if file_name is not None:
            m = parser.match(line)
            if m:
                status = m.group(1)
                has_errors = has_errors or (status == 'error')
                details.append(line)
            else:
                result[file_name] = {
                    'has_errors': has_errors,
                    'details': details,
                }
                file_name = None
                has_errors = False
                details = []
        elif line.startswith(exec_dir):
            file_name = line[len(exec_dir) + 1 :]

    for file_name in result:
        entry = result[file_name]
        test_case = facility.TestCase(
            "{}::eslint".format(file_name),
            Status.FAIL if entry['has_errors'] else Status.GOOD,
            "\n".join([file_name] + entry['details']),
        )
        suite.chunk.tests.append(test_case)
