import os
import logging
import sys

from devtools.ya.test import facility
from devtools.ya.test import const
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test.util import shared
from devtools.ya.test.system import process

logger = logging.getLogger(__name__)


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def on_timeout(signum, frame):
    raise process.SignalInterruptionError()


def get_full_test_name(suite_name, test_name):
    return suite_name + "::" + test_name


def gen_suite(project_path):
    suite = PerformedTestSuite(None, project_path)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def dump_listed_benchmarks(suite_name, tracefile, benchmark_list, project_path):
    suite = gen_suite(project_path)
    for bench in benchmark_list:
        full_test_name = suite_name + "::" + bench
        suite.chunk.tests.append(
            facility.TestCase(full_test_name, const.Status.NOT_LAUNCHED, "test was not launched", path=project_path)
        )
    shared.dump_trace_file(suite, tracefile)
    with open(tracefile, 'r') as afile:
        logger.info("not launched trace: %s", afile.read())


def dump_fail_listing_suite(tracefile, exit_code, project_path, work_dir):
    suite = gen_suite(project_path, work_dir)
    suite.add_chunk_error("Benchmark listing crashed with exit_code: {}".format(exit_code))
    shared.dump_trace_file(suite, tracefile)


def get_suite_name(binary):
    return os.path.splitext(os.path.basename(binary))[0]


def get_full_benchmark_names(suite_name, benchmarks_list):
    return [get_full_test_name(suite_name, x) for x in benchmarks_list]
