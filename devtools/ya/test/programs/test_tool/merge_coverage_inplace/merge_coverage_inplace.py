import os
import sys
import six

import argparse
import json

import exts.fs
import exts.func
import exts.archive
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage
from test import const


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--coverage-paths', default=[], action='append')

    args = parser.parse_args()
    return args


def setup_output(params):
    with open(params.output, "w") as afile:
        afile.write("no valid output was provided")


def get_unified_coverage(coverage_path):
    coverage = {}
    with open(coverage_path) as cov_file:
        for line in cov_file:
            try:
                cur_coverage = json.loads(line)
                coverage[cur_coverage['filename']] = cur_coverage['coverage']
            except ValueError:
                sys.stderr.write("cant load {} as json".format(line))
            except KeyError as ex:
                sys.stderr.write("{} not in {}".format(ex.message, line))
    return coverage


def add_resolved_file(merged_coverage, current_coverage):
    for filename, data in six.iteritems(current_coverage):
        ext = os.path.splitext(filename)[-1]
        if filename not in merged_coverage:
            merged_coverage[filename] = {}
            if ext == ".nlg":
                merged_coverage[filename]['segments'] = lib_coverage.merge.compact_segments(data['segments'])
            else:
                merged_coverage[filename]['segments'] = data['segments']
            merged_coverage[filename]['functions'] = data.get('functions', {})
        if ext in list(const.COVERAGE_PYTHON_EXTS) + [".java", ".nlg"]:
            merged_coverage[filename]['segments'] = lib_coverage.merge.merge_segments(
                merged_coverage[filename]['segments'], current_coverage[filename]['segments']
            )
        else:
            merged_coverage[filename]['segments'] = lib_coverage.merge.merge_granular_coverage_segments(
                merged_coverage[filename]['segments'], current_coverage[filename]['segments']
            )
        lib_coverage.merge.merge_functions_inplace(
            merged_coverage[filename]['functions'], current_coverage[filename].get('functions', {})
        )


def merge_coverage(resolved_coverage_files, output_path):
    merged_coverage = {}
    for coverage_path in resolved_coverage_files:
        current_coverage = get_unified_coverage(coverage_path)
        add_resolved_file(merged_coverage, current_coverage)

    lib_coverage.export.dump_coverage(merged_coverage, output_path)


def extract(cov_filename):
    tmpdir = 'tmp'
    exts.fs.create_dirs(tmpdir)
    exts.archive.extract_from_tar(cov_filename, tmpdir)
    dir_files = os.listdir(tmpdir)
    return [os.path.join(tmpdir, fn) for fn in dir_files]


def get_resolved_files(resolved_coverage_dirs):
    resolved_coverage_files = []
    for dir_name in resolved_coverage_dirs:
        if os.path.exists(dir_name):
            for filename in os.listdir(dir_name):
                if filename.startswith("coverage_resolved") and filename.endswith("json") and ".raw." not in filename:
                    resolved_coverage_files.append(os.path.join(dir_name, filename))
                elif filename == "unified.coverage.tar":
                    resolved_coverage_files = extract(os.path.join(dir_name, filename))
        else:
            sys.stderr.write("flie: {} does not exists".format(dir_name))
    return resolved_coverage_files


def main():
    args = parse_args()
    output_path = args.output
    setup_output(args)
    resolved_coverage_dirs = args.coverage_paths
    resolved_coverage_files = get_resolved_files(resolved_coverage_dirs)

    merge_coverage(resolved_coverage_files, output_path)

    return 0


if __name__ == "__main__":
    exit(main())
