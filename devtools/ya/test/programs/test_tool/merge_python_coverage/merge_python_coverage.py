# coding: utf-8

import os
import sys
import argparse

import coverage

import exts.archive
import exts.tmp
import exts.uniq_id
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output")
    parser.add_argument("--name-filter", default='')
    parser.add_argument("--coverage-path", action="append", dest="coverage_paths", default=[])
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def merge_coverage(params):
    cov_files = []

    for archive_path in params.coverage_paths:
        dirname = exts.uniq_id.gen32()
        assert not os.path.exists(dirname)
        exts.archive.extract_from_tar(archive_path, dirname)
        for filename in os.listdir(dirname):
            if params.name_filter in filename:
                cov_files.append(os.path.join(dirname, filename))

    merge_dir = "merge_{}".format(os.path.basename(params.output))
    os.mkdir(merge_dir)

    if params.verbose:
        debug = ['pid', 'trace', 'sys', 'config']
    else:
        debug = None

    cov_files = lib_coverage.util.get_python_profiles(cov_files)

    if cov_files:
        # Keep filter name in filename for further filters
        data_file = os.path.join(merge_dir, "cov{}".format(params.name_filter))
        cov = coverage.Coverage(data_file=data_file, debug=debug)
        cov.combine(cov_files)
        cov.save()
    else:
        sys.stderr.write("There are no coverage to merge - create empty archive: %s\n" % params.output)
    exts.archive.create_tar(merge_dir, params.output)


def main():
    args = get_options()
    merge_coverage(args)


if __name__ == '__main__':
    main()
