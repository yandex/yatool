# coding: utf-8

import argparse
import os
import sys
import six

import exts.archive
import exts.tmp
import re
from yatest.common import process
from library.python.testing import coverage_utils as coverage_utils_library


def combine_cov_files(cov_files, merge_dir, new_cov_filename, prefix_filter, exclude_regexp):
    """returns merged coverage filename"""
    file_filter = coverage_utils_library.make_filter(prefix_filter, exclude_regexp)
    merged_coverage = {}
    for filename in cov_files:
        with open(filename, 'r') as afile:
            afile.readline()  # skip mode
            for line in afile:
                line = line.strip()
                cover_flag = bool(int(line[-1]))
                segment_info = line[:-1]
                segment_info = re.split(r'[/]', segment_info, maxsplit=1)[1]
                # filtering filenames by prefix and regexp
                if file_filter(extract_filename(segment_info)):
                    if not merged_coverage.get(segment_info):
                        merged_coverage[segment_info] = False
                    merged_coverage[segment_info] = cover_flag | merged_coverage[segment_info]

    merged_coverage_filename = os.path.join(merge_dir, new_cov_filename)
    with open(merged_coverage_filename, 'w') as afile:
        afile.write("mode: set\n")
        for key, value in six.iteritems(merged_coverage):
            afile.write("{}{}\n".format(key, str(int(value))))
    return merged_coverage_filename


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--coverage-tars", default=[], action='append')  # deprecated
    group.add_argument('--merged-coverage-tar')  # , required=True Already merged coverage at input
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--source-root", required=True)
    parser.add_argument('--gotools-path', required=True)
    parser.add_argument("--exclude-regexp")  # deprecated, moved to merge_go_coverage
    parser.add_argument("--prefix-filter")  # deprecated, moved to merge_go_coverage
    return parser.parse_args()


def extract_filename(segment_info):
    return segment_info.split(":")[0]


def resolve_go_tool(path, binname):
    path = os.path.join(path, "pkg/tool")
    names = os.listdir(path)
    # go toolchain contains only one platform
    assert len(names) == 1, names
    return os.path.join(path, names[0], binname)


def build_report(args):
    cwd = os.getcwd()
    tmpdir = 'tmp'
    os.mkdir(tmpdir)

    # TODO: We should switch from using GOPATH before go1.17
    os.environ["GOPATH"] = cwd
    os.environ["GO111MODULE"] = "auto"
    # add symlink to src_root because go html report builder searching go srcs in
    # GOPATH/src/(path to go srcs)
    os.symlink(args.source_root, os.path.join(cwd, "src"))
    os.environ["GOCACHE"] = os.path.join(cwd, tmpdir, ".gocache")

    if args.merged_coverage_tar:
        exts.archive.extract_from_tar(args.merged_coverage_tar, tmpdir)
        merged_coverage_file = os.path.join(tmpdir, "cov")
    else:
        mergedir = 'merge'
        os.mkdir(mergedir)
        for cov_tar in args.coverage_tars:
            exts.archive.extract_from_tar(cov_tar, tmpdir)
        cov_files = [os.path.join(tmpdir, fn) for fn in os.listdir(tmpdir)]
        merged_coverage_file = combine_cov_files(cov_files, mergedir, 'cov', args.prefix_filter, args.exclude_regexp)

    result_dir = "go.coverage.report"
    os.mkdir(result_dir)
    cmd = [
        resolve_go_tool(args.gotools_path, "cover"),
        "-html=" + merged_coverage_file,
        "-o",
        os.path.join(cwd, result_dir, "report.html"),
    ]
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)

    exts.archive.create_tar(result_dir, args.output)


def main():
    args = get_options()
    build_report(args)


if __name__ == '__main__':
    main()
