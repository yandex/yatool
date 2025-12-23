import argparse
import os
import re
import sys

import devtools.ya.test.const
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test import facility
from devtools.ya.test import const
from devtools.ya.test.util import shared
import devtools.ya.test.filter as test_filter


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--binary", required=True, help="Path to the ktlint binary")
    parser.add_argument(
        "-f", "--test-filter", default=[], action="append", help="Run only specified tests (binary name or mask)"
    )
    parser.add_argument("--srclist-path", help="Path to kotlin sources")
    parser.add_argument("--trace-path", help="Path to trace file")
    parser.add_argument("--source-root", help="Path to source root ")
    parser.add_argument("--build-root", help="Path to build root ")
    parser.add_argument("--project-path", help="Path to source root ")
    parser.add_argument("--output-dir", help="Path to source root ")
    parser.add_argument("--tests-filter", help="Path to source root ")
    parser.add_argument("--test-list", help="Is test required for listing only", action="store_true")
    parser.add_argument("--editorconfig", help="Use editorconfig ktlint folder name")
    parser.add_argument("--baseline", help="Path to baseline for ktlint test")
    parser.add_argument("--ruleset", help="Path to ktlint ruleset")

    args = parser.parse_args()
    return args


def colorize(line):
    return re.sub(
        r"^(.*?):(\d+):(\d+): (.*?)$",
        r"[[unimp]]\1[[rst]]:[[alt2]]\2[[rst]]:[[alt2]]\3[[rst]]: [[bad]]\4[[rst]]",
        line,
    )


def gen_suite(project_path):
    suite = PerformedTestSuite(None, project_path)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def get_full_ktlint_name(test_cast_name):
    return "ktlint::{}".format(test_cast_name)


def fill_suite_with_deselected_tests(suite, deselected_tests, args):
    for deselected_test_case in deselected_tests:
        test_case = facility.TestCase(
            deselected_test_case, const.Status.DESELECTED, path=args.project_path, logs={'logsdir': args.output_dir}
        )
        suite.chunk.tests.append(test_case)


def get_test_list(args):
    with open(args.srclist_path, 'r') as afile:
        tests_list = afile.read().split()
        tests_list = [t for t in tests_list if t.endswith(".kt")]
    return tests_list


def run_ktlint(suite, tests_to_run, args):
    ktlint_stderr = os.path.join(args.output_dir, "ktlint.err")
    ktlint_stdout = os.path.join(args.output_dir, "ktlint.out")

    # I expected to have arcadia root in args.source_root but it contains path to build_root somehow...
    cmd = [args.binary, "--editorconfig=" + args.editorconfig, "--relative"]
    cmd += list(tests_to_run.keys())
    if args.baseline:
        cmd += ["--baseline=" + args.baseline]
    if args.ruleset:
        cmd += ["--ruleset=" + os.path.join(args.build_root, args.ruleset)]
    os.chdir(args.source_root)
    shared.tee_execute(cmd, ktlint_stdout, ktlint_stderr, strip_ansi_codes=True)
    errors = {}
    with open(ktlint_stdout, 'r') as afile:
        for line in afile:
            error_name = line.split(":", 1)[0]
            if error_name not in errors:
                errors[error_name] = []
            errors[error_name].append(line)
    for test_name, full_test_name in tests_to_run.items():
        status = devtools.ya.test.const.Status.FAIL if test_name in errors else devtools.ya.test.const.Status.GOOD
        message = ""
        if status == devtools.ya.test.const.Status.FAIL:
            message = '\n' + '\n'.join([colorize(line.strip()) for line in errors[test_name]])
        suite.chunk.tests.append(facility.TestCase(full_test_name, status, message, path=args.project_path))


def filter_tests(tests_map, tests_filter):
    filter_func = test_filter.make_testname_filter(tests_filter)
    res_map = {name: full_name for name, full_name in tests_map.items() if filter_func(full_name)}
    return res_map


def main():
    args = parse_args()
    suite = gen_suite(args.project_path)
    tests_list = get_test_list(args)
    tests_full_name_map = {test: get_full_ktlint_name(test) for test in tests_list}
    if args.test_filter:
        tests_full_name_map = filter_tests(tests_full_name_map, args.test_filter)
    if args.test_list:
        sys.stderr.write("\n".join(sorted(tests_full_name_map.values())))
    fill_suite_with_deselected_tests(suite, tests_full_name_map.values(), args)
    run_ktlint(suite, tests_to_run=tests_full_name_map, args=args)
    shared.dump_trace_file(suite, args.trace_path)


if __name__ == "__main__":
    exit(main())
