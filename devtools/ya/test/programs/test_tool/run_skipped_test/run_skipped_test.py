import sys
import logging
import argparse

import devtools.ya.test.common
import devtools.ya.test.test_types.common
import devtools.ya.test.const


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace-path", help="Path to the output trace log")
    parser.add_argument("--reason", help="Skipping reason")

    args = parser.parse_args()
    setup_logging(False)

    suite = devtools.ya.test.test_types.common.PerformedTestSuite(None, None, None)
    suite.add_suite_error(args.reason, status=devtools.ya.test.const.Status.SKIPPED)
    suite.generate_trace_file(args.trace_path)

    return 0


if __name__ == "__main__":
    exit(main())
