import argparse
import json
import logging
import os
import sys

import devtools.ya.test.common
import devtools.ya.test.filter
import devtools.ya.test.test_types.common
from devtools.ya.test import facility
from devtools.ya.test.system import process


STATUSES = {
    "GOOD": devtools.ya.test.common.Status.GOOD,
    "FAIL": devtools.ya.test.common.Status.FAIL,
    "SKIPPED": devtools.ya.test.common.Status.SKIPPED,
}


logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=False, default=None)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--project-path", required=True)
    parser.add_argument("--trace-path", help="Path to the output trace log")
    parser.add_argument("--out-path", help="Path to the output test_cases")
    parser.add_argument("--tests-filters", required=False, action="append")
    parser.add_argument("--lint-name", help="Lint name")
    parser.add_argument("--linter", required=True, help="Path to linter binary (optional")
    parser.add_argument("--wrapper-script", required=False, help="Path to wrapper script")
    parser.add_argument("--depends", required=False, action="append", help="Depends. The option can be repeated")
    parser.add_argument(
        "--global-resource",
        required=False,
        dest="global_resources",
        action="append",
        help="Global resource. Format 'var_name::resources_path'. The option can be repeated",
    )
    parser.add_argument(
        "--config",
        required=False,
        dest="configs",
        action="append",
        help="Configuration file. The option can be repeated",
    )
    parser.add_argument(
        "--extra-param", required=False, dest="extra_params", action="append", help="Additional linter parameters"
    )
    parser.add_argument('files', nargs='*')
    return parser.parse_args()


def get_test_name(lint_name, project_path, filename):
    relative_path = os.path.relpath(filename, project_path)
    return "{}::{}".format(relative_path, lint_name)


def main():
    args = parse_args()
    logging.basicConfig(level=logging.DEBUG, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")

    output_path = args.out_path
    suite = devtools.ya.test.test_types.common.PerformedTestSuite(None, None, None)
    suite.register_chunk()

    logger.debug("Test filters: %s", args.tests_filters)
    if args.tests_filters:
        filter_func = devtools.ya.test.filter.make_testname_filter(args.tests_filters)
    else:
        filter_func = _dummy_filter

    test_cases = []
    for file_name in args.files:
        test_name = get_test_name(args.lint_name, args.project_path, file_name)
        if not filter_func(test_name):
            continue
        test_cases.append((file_name, test_name))

    if test_cases:
        linter_params_file = os.path.join(output_path, "linter_params.json")
        linter_report_file = os.path.join(output_path, "linter_report.json")
        global_resources = _parse_kv_arg(args.global_resources, "::")
        extra_params = _parse_kv_arg(args.extra_params, "=")

        linter_params = {
            "source_root": args.source_root,
            "project_path": args.project_path,
            "output_path": output_path,
            "lint_name": args.lint_name,
            "files": [f for f, _ in test_cases],
            "depends": {dep: os.path.join(args.build_root, dep) for dep in args.depends},
            "global_resources": global_resources,
            "configs": args.configs,
            "extra_params": extra_params,
            "report_file": linter_report_file,
        }
        with open(linter_params_file, "w") as f:
            json.dump(linter_params, f)

        if args.wrapper_script:
            test_tool = sys.executable
            wrapper_script = os.path.join(args.source_root, args.wrapper_script)
            env = os.environ.copy()
            env['Y_PYTHON_ENTRY_POINT'] = ':main'
            res = process.execute(
                [test_tool, wrapper_script, "--params", linter_params_file], check_exit_code=False, env=env
            )
        else:
            linter_path = os.path.join(args.build_root, args.linter)
            res = process.execute([linter_path, "--params", linter_params_file], check_exit_code=False)

        if res.exit_code:
            logger.error("Linter return exit code={}".format(res.exit_code))
            return 1

        try:
            with open(linter_report_file, "r") as f:
                report_data = json.load(f)

            report = report_data.get("report")
            if report is None:
                raise Exception("Wrong lint report: 'report' key doesn't exist")

            rel_project_path = os.path.relpath(args.project_path, args.source_root)
            for file_name, test_name in test_cases:
                file_report = report.get(file_name, {})
                status = file_report.get("status", "GOOD")
                elapsed = file_report.get("elapsed", 0.0)
                message = file_report.get("message", "")
                test_status = STATUSES.get(status)
                if test_status is None:
                    raise ValueError(
                        "Unknown status: '{}'. Expected one of: {}".format(status, ",".join(STATUSES.keys()))
                    )

                suite.chunk.tests.append(
                    facility.TestCase(
                        test_name,
                        test_status,
                        message,
                        logs={"logsdir": output_path},
                        path=rel_project_path,
                        elapsed=elapsed,
                    )
                )
        except Exception as e:
            logger.exception("Cannot parse linter report file '{}': {}".format(linter_report_file, e))
            return 1

    suite.set_work_dir(os.getcwd())
    suite.generate_trace_file(args.trace_path)

    return 0


def _dummy_filter(*args):
    return True


def _parse_kv_arg(arg, sep):
    result = {}
    if arg:
        for item in arg:
            assert sep in item
            var, val = item.split(sep, 1)
            result[var] = val
    return result
