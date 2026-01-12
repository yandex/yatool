import argparse
import json
import logging
import os
import re

import build.plugins.lib.nots.package_manager.constants as pm_const
import build.plugins.lib.nots.test_utils.ts_utils as ts_utils

import devtools.ya.test
import devtools.ya.test.const
import devtools.ya.test.facility
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
    parser.add_argument("--test-config", help="Biome config filename", required=True)
    parser.add_argument("--trace", help="Path to the output trace log", required=True)

    parser.add_argument("files", nargs='*')

    return parser.parse_args(argv)


def get_biome_cmd(args):
    cmd = [
        os.path.join(args.nodejs_dir, "node"),
        os.path.join(args.build_root, args.project_path, "node_modules", "@biomejs", "biome", "bin", "biome"),
        'ci',
        '--reporter=github',
    ]

    cmd = cmd + args.files
    return cmd


def parse_github_output(output, build_dir):
    pattern = re.compile(
        r'^::(warning|error)\s+'
        r'title=([^,]+),'
        r'file=([^,]+),'
        r'line=(\d+)'
        r'(?:,endLine=(\d+))?'
        r'(?:,col=(\d+))?'
        r'(?:,endColumn=(\d+))?'
        r'::(.+)$'
    )

    files_map = {}

    for line in output.strip().split('\n'):
        match = pattern.match(line)

        if not match:
            continue

        severity, category, filepath, line_num, end_line, col, end_col, message = match.groups()

        if os.path.isabs(filepath):
            rel_path = os.path.relpath(filepath, build_dir)
        else:
            rel_path = filepath

        diagnostics = {
            'severity': severity,
            'category': category,
            'message': message,
            'location': {
                'line': int(line_num),
                'column': int(col) if col else 0,
            },
        }

        if rel_path not in files_map:
            files_map[rel_path] = []
        files_map[rel_path].append(diagnostics)

    return files_map


def format_error(diagnostics: list[dict], file_path: str = None):
    if not diagnostics:
        return ""

    RED = '\033[31m'
    YELLOW = '\033[33m'
    RESET = '\033[0m'

    rows = []

    if file_path:
        rows.append(file_path)

    max_message_len = 0
    for d in diagnostics:
        message = d.get('message', 'Unknown error')
        max_message_len = max(max_message_len, len(message))

    for d in diagnostics:
        severity = d.get('severity', 'error')
        category = d.get('category', '')
        message = d.get('message', 'Unknown error')
        location = d.get('location', {})
        line = location.get('line', 0)
        col = location.get('column', 0)

        if severity == "error":
            color = RED
            label = "error  "
        else:
            color = YELLOW
            label = "warning"

        if category == 'format':
            pos = "-:-"
        elif line and col:
            pos = f"{line}:{col}"
        elif line:
            pos = f"{line}:1"
        else:
            pos = "1:1"

        pos_formatted = f"{pos:>6}"

        message_padded = f"{message:<{max_message_len + 2}}"

        if category:
            cat_text = f"{category}"
            rows.append(f"{pos_formatted}  {color}{label}{RESET}  {message_padded}{cat_text}")
        else:
            rows.append(f"{pos_formatted}  {color}{label}{RESET}  {message}")

    count = len(diagnostics)

    if not file_path:
        if count > 1:
            title = f"There are {count} problems:\n"
        else:
            title = "There is one problem:\n"
        rows.insert(0, title.rstrip('\n'))

    result = "\n".join(rows)

    if file_path:
        result = "\n" + result

    return result


def fill_suite(build_dir, files_map, requested_files, trace):
    suite = devtools.ya.test.test_types.common.PerformedTestSuite(None, None, None)
    suite.set_work_dir(build_dir)
    suite.register_chunk()

    result = {}
    for file_name in requested_files:
        result[file_name] = {
            'has_errors': False,
            'details': [],
        }

    for file_path, diags in files_map.items():
        if file_path in result:
            has_errors = any(d.get('severity') == 'error' for d in diags)
            result[file_path]['has_errors'] = has_errors
            result[file_path]['details'] = diags

    for file_name in sorted(result.keys()):
        entry = result[file_name]

        status = devtools.ya.test.const.Status.FAIL if entry['has_errors'] else devtools.ya.test.const.Status.GOOD

        if entry['details']:
            comment = format_error(entry['details'], file_path=file_name)
        else:
            comment = file_name

        test_case = devtools.ya.test.facility.TestCase(
            f"{file_name}::ts_biome",
            status,
            comment,
        )

        suite.chunk.tests.append(test_case)

    suite.generate_trace_file(trace)


def main():
    args = parse_args()

    src_dir = os.path.join(args.source_root, args.project_path)
    build_dir = os.path.join(args.build_root, args.project_path)

    ts_utils.copy_dir_contents(
        src_dir,
        build_dir,
        ignore_list=[
            pm_const.BUILD_DIRNAME,
            pm_const.BUNDLE_DIRNAME,
            pm_const.NODE_MODULES_DIRNAME,
            pm_const.NODE_MODULES_WORKSPACE_BUNDLE_FILENAME,
            pm_const.OUTPUT_TAR_FILENAME,
            pm_const.PACKAGE_JSON_FILENAME,
            pm_const.PNPM_LOCKFILE_FILENAME,
        ],
    )

    exec_result = devtools.ya.test.system.process.execute(get_biome_cmd(args), cwd=build_dir, check_exit_code=False)

    if exec_result.exit_code < 0:
        logger.error("biome was terminated by signal: %s", exec_result.exit_code)
        return 1

    try:
        files_map = parse_github_output(exec_result.std_out, build_dir)
    except json.decoder.JSONDecodeError:
        logger.error("biome failed with the error:\n%s", exec_result.std_out + exec_result.std_err)
        return 1

    fill_suite(build_dir, files_map, args.files, args.trace)

    return 0
