import argparse
import glob
import logging
import os
import sys
import time

from devtools.ya.test import facility
from test.util import shared
import devtools.ya.test.programs.test_tool.lib.runtime as runtime
import library.python.cores as cores
import test.common
import test.const
import test.filter as test_filter
import test.system.process
import test.test_types.common
import test.util.shared

logger = logging.getLogger(__name__)


def setup_logging():
    logging.basicConfig(level=logging.DEBUG, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def safe_read(filename):
    try:
        with open(filename) as afile:
            return afile.read()
    except OSError:
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('test_cases', nargs='*')
    parser.add_argument("--tests-filters", required=False, action="append")
    parser.add_argument("--source-root", required=False, default=None)
    parser.add_argument("--checker", required=True, help="Path to the pep8_checker binary")
    parser.add_argument("--check-name", required=True, help="Name of the check")
    parser.add_argument("--trace-path", help="Path to the output trace log")
    parser.add_argument("--out-path", help="Path to the output test_cases")
    parser.add_argument("--list", action="store_true", help="List of tests", default=False)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--no-snippet-from-stdout", dest="snippet_from_stdout", action="store_false", help="Don't add checker stdout to the subtest snippet", default=True)
    parser.add_argument("--no-snippet-from-stderr", dest="snippet_from_stderr", action="store_false", help="Don't add checker stderr to the subtest snippet", default=True)
    parser.add_argument("--batch", action="store_true", default=False)
    parser.add_argument("--batch-name", action="store", default=None)
    parser.add_argument("--token", dest="token", action='store', help="access token")
    parser.add_argument("--token-path", dest="token_path", action='store', help="path to access token")
    parser.add_argument("--file-pattern", action="store", default="*")

    args = parser.parse_args()
    setup_logging()

    logger.debug("run_check has started")

    def test_run_args(case):
        test_path = os.path.dirname(os.path.relpath(case, args.source_root))
        if ".." in test_path:
            test_path = None
        return os.path.basename(case), test_path, [case]

    def get_test_cases():
        test_cases = []
        if args.batch and args.test_cases:
            test_cases.append((args.batch_name, None, args.test_cases))
        else:
            for case in args.test_cases:
                if os.path.isdir(case):
                    for file_name in glob.glob1(case, args.file_pattern):
                        test_cases.append(test_run_args(os.path.join(case, file_name)))
                else:
                    test_cases.append(test_run_args(case))

        if test_cases and args.tests_filters:
            filter_func = test_filter.make_testname_filter(args.tests_filters)
            test_cases = [tc for tc in test_cases if filter_func("{}::{}".format(tc[0], args.check_name))]

        return test_cases

    if args.list:
        sys.stdout.write(os.linesep.join([os.path.basename(p[0]) for p in get_test_cases()]))
        return 0

    logs_dir = args.out_path

    tests = []
    for test_name, test_path, checker_args in get_test_cases():
        cmd = [sys.executable] + args.checker.split(" ") + checker_args + test.util.shared.get_oauth_token_options(args, test_tool_mode=True)

        out_path = test.common.get_unique_file_path(logs_dir, "{}.{}.out".format(test_name, args.check_name))
        err_path = test.common.get_unique_file_path(logs_dir, "{}.{}.err".format(test_name, args.check_name))

        started = time.time()
        with runtime.bypass_signals(["SIGQUIT", "SIGUSR2"]) as reg:
            res = shared.tee_execute(cmd, out_path, err_path, strip_ansi_codes=True, on_startup=lambda proc: reg.register(proc.pid))

        duration = time.time() - started

        out = safe_read(out_path)
        err = safe_read(err_path)

        logs = {'logsdir': logs_dir}

        if out:
            logs["stdout"] = out_path
        if err:
            logs["stderr"] = err_path

        snippet = ""
        if res.returncode != 0:
            if args.snippet_from_stdout:
                snippet += out
            if args.snippet_from_stderr:
                snippet += err

        test_case = facility.TestCase(
            "{}::{}".format(test_name, args.check_name),
            test.const.Status.GOOD if res.returncode == 0 else test.const.Status.FAIL,
            cores.colorize_backtrace(snippet),
            duration,
            logs=logs,
            path=test_path,
        )
        tests.append(test_case)

    logger.debug("testing completed")

    suite = test.test_types.common.PerformedTestSuite(None, None, None)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    suite.chunk.tests = tests
    suite.generate_trace_file(args.trace_path)

    return 0


if __name__ == "__main__":
    exit(main())
