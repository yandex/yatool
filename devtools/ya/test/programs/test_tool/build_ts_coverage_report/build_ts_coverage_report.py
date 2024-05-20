# coding: utf-8

import argparse
import logging
import os
import shutil
import exts.archive
import sys
from test.util import shared
from yatest.common import process

logger = logging.getLogger(__name__)


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output")
    parser.add_argument("--nodejs-resource", required=True)
    parser.add_argument("--nyc-resource", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--coverage-tars", default=[], action='append')
    parser.add_argument("--log-path", required=True)
    parser.add_argument("--log-level", default="INFO")
    return parser.parse_args()


def _makedirs(dir_path):
    try:
        os.makedirs(dir_path)
    except OSError:
        pass


def extract_coverage_final_files(coverage_tars, target_directory):
    cov_file_index = 0
    for cov_resolve_result in coverage_tars:
        if not cov_resolve_result.endswith(".tar"):
            continue

        def _entry_filter(entry):
            logger.debug("member filename: {}".format(entry.pathname))
            return entry.pathname == "coverage-final.json"

        exts.archive.extract_from_tar(cov_resolve_result, target_directory, entry_filter=_entry_filter)
        extracted_filename = os.path.join(target_directory, "coverage-final.json")
        if os.path.exists(extracted_filename):
            tmp_filename = os.path.join(target_directory, "{}.json".format(cov_file_index))
            shutil.move(extracted_filename, tmp_filename)
            logger.debug("tmp_filename: {}".format(tmp_filename))
            cov_file_index += 1


def build_report(params):
    logger.info("Building ts coverage report")
    work_dir = os.path.realpath(".")
    tmpdir = os.path.join(work_dir, "tmp")
    cov_merge_dir = os.path.join(work_dir, "merge")
    tmp_merged_report_dir = os.path.join(work_dir, "result_report")
    merged_coverage_filename = os.path.join(cov_merge_dir, "result_merged_ts_coverage.json")

    nodejs_bin = os.path.join(params.nodejs_resource, "node")
    nyc_module = os.path.join(params.nyc_resource, "node_modules", "nyc", "bin", "nyc.js")

    _makedirs(tmpdir)
    _makedirs(cov_merge_dir)
    _makedirs(tmp_merged_report_dir)

    extract_coverage_final_files(params.coverage_tars, tmpdir)

    # nyc merge cov_dir result_merge/merged-coverage.json
    cmd = [nodejs_bin, nyc_module, "merge", tmpdir, merged_coverage_filename]
    logger.debug("Results merge cmd: {}".format(cmd))
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)

    # nyc report -t result_merge --report-dir result_report --reporter html
    cmd = [
        nodejs_bin,
        nyc_module,
        "report",
        "-t",
        cov_merge_dir,
        "--report-dir",
        tmp_merged_report_dir,
        "--reporter",
        "html",
    ]
    logger.debug("Report cmd: {}".format(cmd))
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)

    exts.archive.create_tar(tmp_merged_report_dir, params.output)

    logger.info("TS coverage report created: {}".format(params.output))


def main():
    args = get_options()
    shared.setup_logging(args.log_level, args.log_path)
    build_report(args)


if __name__ == '__main__':
    main()
