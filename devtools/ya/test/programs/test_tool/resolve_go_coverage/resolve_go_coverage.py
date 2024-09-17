import re
import os
import logging
import six
import functools

import argparse

import exts.fs
import exts.func
import exts.archive
from devtools.ya.test.programs.test_tool.lib import runtime
from devtools.ya.test.util import shared
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage
from library.python.testing import coverage_utils as coverage_utils_library

logger = logging.getLogger(__name__)


def combine_cov_files(cov_files, merge_dir, new_cov_filename):
    """returns merged coverage filename"""
    merged_coverage = {}
    for filename in cov_files:
        with open(filename, 'r') as afile:
            afile.readline()  # skip mode
            for line in afile:
                line = line.strip()
                cover_flag = bool(int(line[-1]))
                segment_info = line[:-1]
                if not merged_coverage.get(segment_info):
                    merged_coverage[segment_info] = False
                merged_coverage[segment_info] = cover_flag | merged_coverage[segment_info]
    merged_coverage_filename = os.path.join(merge_dir, new_cov_filename)
    with open(merged_coverage_filename, 'w') as afile:
        afile.write("mode: set\n")
        for key, value in six.iteritems(merged_coverage):
            afile.write(key + str(int(value)) + '\n')
    return merged_coverage_filename


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--coverage-path', required=True)
    parser.add_argument('--log-path')
    parser.add_argument('--log-level', default='INFO')
    parser.add_argument("--exclude-regexp")
    parser.add_argument("--prefix-filter")

    args = parser.parse_args()
    return args


def setup_env(params):
    shared.setup_logging(params.log_level, params.log_path)
    # create node's output
    with open(params.output, "w") as afile:
        afile.write("no valid output was provided")


def extract(cov_filename):
    tmpdir = 'tmp'
    exts.fs.create_dirs(tmpdir)
    exts.archive.extract_from_tar(cov_filename, tmpdir)
    dir_files = os.listdir(tmpdir)
    return [os.path.join(tmpdir, fn) for fn in dir_files]


def parse_coverage(coverage_path, prefix_filter, exclude_regexp):
    file_filter = coverage_utils_library.make_filter(prefix_filter, exclude_regexp)
    cov = []  # coverage format: filename start_line start_offset end_line end_offset cover_flag
    regex = re.compile(r"a.yandex-team.ru/(.*?):(\d+)\.(\d+),(\d+)\.(\d+)\s+(\d+)\s+(\d+)")
    with open(coverage_path, "r") as afile:
        afile.readline()  # skip coverage mode
        for line in afile:
            line = line.strip()
            match = regex.search(line)
            assert match, "bad line\n"
            filename, spos, sshift, epos, eshift, _, covered = match.groups()
            # filtering filenames by prefix and regexp
            if file_filter(filename):
                cov.append((filename, [int(spos) - 1, int(sshift), int(epos) - 1, int(eshift), int(covered)]))
    return cov


def segment_comp(lhs, rhs):
    if lhs[0] != rhs[0]:
        return lhs[0] - rhs[0]  # compare start lines
    else:
        return lhs[1] - rhs[1]  # compare start pos(lhs[0] == rhs[0])


def transform_coverage(go_cov):
    uni_cov = {}
    for filename, segment in go_cov:
        if not uni_cov.get(filename):
            uni_cov[filename] = {"segments": [], "functions": {}}
        uni_cov[filename]["segments"].append(segment)

    for filename, cov in six.iteritems(uni_cov):
        cov["segments"].sort(key=functools.cmp_to_key(segment_comp))

    return uni_cov


def resolve_covergage(cov_path, output, prefix_filter, exclude_regexp):
    go_cov = parse_coverage(cov_path, prefix_filter, exclude_regexp)
    unified_cov = transform_coverage(go_cov)
    lib_coverage.export.dump_coverage(unified_cov, output)


def main():
    args = parse_args()
    setup_env(args)
    dir_files = extract(args.coverage_path)
    merge_dir = 'merge'
    cov_fn = "cov"
    os.mkdir(merge_dir)
    merged_coverage_filename = combine_cov_files(dir_files, merge_dir, cov_fn)
    resolve_covergage(merged_coverage_filename, args.output, args.prefix_filter, args.exclude_regexp)

    logger.debug('maxrss: %d', runtime.get_maxrss())
    return 0


if __name__ == "__main__":
    exit(main())
