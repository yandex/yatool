import argparse
import json
import logging
import os

import build.plugins.lib.nots.package_manager.base.constants as pm_const
import build.plugins.lib.nots.package_manager.pnpm.constants as pnpm_const
import build.plugins.lib.nots.typescript as nots_typescript

import devtools.ya.test
import devtools.ya.test.const
import devtools.ya.test.system.process
import devtools.ya.test.test_types.common
import devtools.ya.test.util


logger = logging.getLogger(__name__)


def parse_args(argv=None):
    parser = argparse.ArgumentParser()

    parser.add_argument("--source-root", help="Source root", required=True)
    parser.add_argument("--build-root", help="Build root", required=True)
    parser.add_argument("--project-path", required=True)
    parser.add_argument("--nodejs-dir", help="Path to the Node.js resource", required=True)
    parser.add_argument("--test-config", help="Stylelint config filename", required=True)
    parser.add_argument("--trace", help="Path to the output trace log", required=True)

    parser.add_argument("files", nargs='*')

    return parser.parse_args(argv)


def get_stylelint_cmd(args):
    return [
        os.path.join(args.nodejs_dir, "node"),
        os.path.join(args.build_root, args.project_path, "node_modules", "stylelint", "bin", "stylelint.mjs"),
        '--config',
        args.test_config,
        '--formatter',
        'json',
    ] + args.files


def format_error(records: list[dict]):
    """
    The record example is:
         {
            "line": 7,
            "column": 32,
            "endLine": 7,
            "endColumn": 36,
            "rule": "alpha-value-notation",
            "severity": "error",
            "text": "Expected \"0.87\" to be \"87%\" (alpha-value-notation)"
        }
    """
    count = len(records)
    if count == 0:
        return ""

    sorted_by_pos = sorted(records, key=lambda w: (w['line'], w['column']))
    rows = []
    for r in sorted_by_pos:
        pos = f"{r['line']:>4}:{r['column']:<3}"
        icon = "❌" if r['severity'] == 'error' else '⚠️'
        text = r['text']

        rows.append("  ".join([pos, icon, text]))

    title = f"There are {count} errors:\n" if count > 1 else "There is one error:\n"
    footer = "\n\nRun 'ya tool nots exec stylelint --fix' for fix, if possible."

    return title + "\n".join(rows) + footer


def fill_suite(build_dir, report_json, trace):
    suite = devtools.ya.test.test_types.common.PerformedTestSuite(None, None, None)
    suite.set_work_dir(build_dir)
    suite.register_chunk()

    for case in report_json:
        file = os.path.relpath(case['source'], build_dir)

        warnings_ = case['warnings']
        has_errors = len(warnings_) > 0
        status = devtools.ya.test.const.Status.FAIL if has_errors else devtools.ya.test.const.Status.GOOD

        test_case = devtools.ya.test.facility.TestCase(
            f"{file}::ts_stylelint",
            status,
            format_error(warnings_),
        )

        suite.chunk.tests.append(test_case)

    suite.generate_trace_file(trace)


def main():
    # devtools.ya.test.util.shared.setup_logging("DEBUG", "")

    args = parse_args()

    src_dir = os.path.join(args.source_root, args.project_path)
    build_dir = os.path.join(args.build_root, args.project_path)

    devtools.ya.test.util.tools.copy_dir_contents(
        src_dir,
        build_dir,
        ignore_list=[
            pm_const.BUILD_DIRNAME,
            pm_const.BUNDLE_DIRNAME,
            pm_const.NODE_MODULES_DIRNAME,
            pm_const.NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            pm_const.OUTPUT_TAR_UUID_FILENAME,
            pm_const.PACKAGE_JSON_FILENAME,
            pnpm_const.PNPM_LOCKFILE_FILENAME,
            nots_typescript.DEFAULT_TS_CONFIG_FILE,
        ],
    )

    exec_result = devtools.ya.test.system.process.execute(get_stylelint_cmd(args), cwd=build_dir, check_exit_code=False)
    if exec_result.exit_code < 0:
        logger.error("stylelint was terminated by signal: %s", exec_result.exit_code)
        return 1

    try:
        # https://stylelint.io/migration-guide/to-16#changed-cli-to-print-problems-to-stderr
        output = exec_result.std_err if exec_result.std_err else exec_result.std_out
        report_json = json.loads(output)
    except json.decoder.JSONDecodeError:
        logger.error("stylelint failed with the error:\n%s", exec_result.std_out + exec_result.std_err)
        return 1

    fill_suite(build_dir, report_json, args.trace)

    return 0
