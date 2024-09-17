# coding: utf-8

from __future__ import print_function
import os
import sys
import json
import logging
import argparse
import subprocess

from devtools.ya.test.util import shared
from devtools.ya.test import const
from devtools.ya.test import facility
from devtools.ya.test.test_types.common import PerformedTestSuite

logger = logging.getLogger(__name__)

SUCCESSFULLY_DUMPED_RC = 15


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, help="Path to the fuzz binary")
    parser.add_argument("--tracefile", required=True, help="Path to the output trace log")
    parser.add_argument("--output-dir", required=True, help="Path to the output dir")
    parser.add_argument("--project-path", help="Project path relative to arcadia")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--list", action="store_true", help="List of tests")

    args = parser.parse_args()
    args.binary = os.path.abspath(args.binary)
    if not args.project_path:
        path = os.path.dirname(args.binary)
        if "arcadia" not in path:
            parser.error("Failed to determine project path")
        args.project_path = path.rsplit("arcadia")[1]
    args.project_path.strip("/")
    return args


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def gen_suite(project_path):
    suite = PerformedTestSuite(None, project_path)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def setup_env():
    os.environ["YA_COVERAGE_DUMP_PROFILE_AND_EXIT"] = "1"
    os.environ["YA_COVERAGE_DUMP_PROFILE_EXIT_CODE"] = str(SUCCESSFULLY_DUMPED_RC)


def main():
    args = parse_args()
    testname = "coverage_extractor::test"

    if args.list:
        print(testname)
        return 0

    setup_logging(args.verbose)
    setup_env()
    logger.debug("Environment variables: %s", json.dumps(dict(os.environ), sort_keys=True, indent=4))

    open(args.tracefile, "w").close()
    suite = gen_suite(args.project_path)

    proc = subprocess.Popen([args.binary], stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
    err = proc.communicate()[0]
    rc = proc.wait()
    if rc == SUCCESSFULLY_DUMPED_RC:
        suite.chunk.tests.append(facility.TestCase(testname, const.Status.GOOD))
    else:
        suite.chunk.add_error("[[bad]]Binary failed with {} return code: {}".format(rc, err), const.Status.FAIL)
    shared.dump_trace_file(suite, args.tracefile)
    return 0


if __name__ == "__main__":
    exit(main())
