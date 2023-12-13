# coding: utf-8

import argparse
import logging
import os
import tarfile
import ujson as json
from test.util import shared

logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-path")
    parser.add_argument("--output", required=True)
    parser.add_argument("--coverage-path", required=True)
    parser.add_argument("--log-path", required=True)
    parser.add_argument("--log-level", default="INFO")

    args = parser.parse_args()
    return args


def _makedirs(dir_path):
    try:
        os.makedirs(dir_path)
    except OSError:
        pass


def extract(archive_filename):
    tmpdir = "tmp"
    _makedirs(tmpdir)
    with tarfile.open(archive_filename, "r") as fh:
        fh.extractall(tmpdir)
    return tmpdir


def extract_coverage_file(arch_path):
    logger.debug("Extracting archived data " + arch_path)
    datadir = extract(arch_path)
    cov_files = [filename for filename in os.listdir(datadir) if filename.startswith("coverage-final")]

    assert len(cov_files) <= 1, "Found several coverage input files: {}".format(cov_files)

    return os.path.join(datadir, cov_files[0])


def setup_env(params):
    shared.setup_logging(params.log_level, params.log_path)


# Columns could be None in jest cov data, just replacing those with 0 if any to avoid confusion
def _num_from_nullable(coordinate):
    return coordinate if coordinate else 0


def file_coverage_to_unified_format(filename, file_cov_info):
    res_file_cov_info = {"coverage": {"functions": {}, "segments": []}, "filename": filename}
    for f_id, f_info in file_cov_info["fnMap"].items():
        f_info_start = f_info["decl"]["start"]
        func_start_location = "({}, {})".format(f_info_start["line"], _num_from_nullable(f_info_start["column"]))
        res_cov_functions = res_file_cov_info["coverage"]["functions"]
        if func_start_location not in res_cov_functions:
            res_cov_functions[func_start_location] = {}
        res_cov_functions[func_start_location][f_info["name"]] = file_cov_info["f"][f_id]

    res_cov_segments = res_file_cov_info["coverage"]["segments"]
    for s_id, s_info in file_cov_info["statementMap"].items():
        segment_start, segment_end = s_info["start"], s_info["end"]
        start_column = _num_from_nullable(segment_start["column"])
        end_column = _num_from_nullable(segment_end["column"])
        res_cov_segments.append(
            [
                segment_start["line"] - 1,
                start_column - 1 if start_column > 0 else 0,
                segment_end["line"] - 1,
                end_column - 1 if end_column > 0 else 0,
                1 if file_cov_info["s"][s_id] else 0,
            ]
        )
    # Easier to check manually if we have it sorted
    res_file_cov_info["coverage"]["segments"] = sorted(res_cov_segments)

    return res_file_cov_info


def resolve_coverage(input_cov_data_filename, args):
    logger.debug("Resolving coverage for" + input_cov_data_filename)
    with open(input_cov_data_filename, "r") as input_cov_data_file:
        coverage_data = json.load(input_cov_data_file)

    res_arc_cov_info = []
    res_jest_cov_info = {}
    for filename, file_cov_info in coverage_data.items():
        # Update paths in coverity results
        # (different suites could give different paths based on temporary src root dir - removing it)
        src_root_index = filename.find(args.project_path)
        local_filename = filename[src_root_index:] if src_root_index > 0 else filename

        res_file_cov_info = file_coverage_to_unified_format(local_filename, file_cov_info)

        res_arc_cov_info.append(res_file_cov_info)
        # Create a new jest results dict with updated src files paths
        file_cov_info["path"] = local_filename
        res_jest_cov_info[local_filename] = file_cov_info
        logger.debug("Adding data for " + local_filename + ", cov_info: " + str(file_cov_info))

    # Update src file paths in the jest results file
    with open(input_cov_data_filename, "w") as jest_results_file:
        json.dump(res_jest_cov_info, jest_results_file)

    # Dump arc coverage results
    with open(args.output, "w") as arc_cov_results_file:
        for covdata in res_arc_cov_info:
            json.dump(covdata, arc_cov_results_file)
            arc_cov_results_file.write("\n")


def main():
    args = parse_args()
    setup_env(args)

    data_file = extract_coverage_file(args.coverage_path)
    # test might be skipped by filter and there will be no coverage data
    if not data_file:
        logger.debug("No cov data available")
        with open(args.output, "w") as res_cov_file:
            json.dump({}, res_cov_file)
        return

    resolve_coverage(data_file, args)

    return 0


if __name__ == "__main__":
    exit(main())
