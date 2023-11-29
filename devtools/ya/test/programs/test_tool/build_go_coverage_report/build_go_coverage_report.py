# coding: utf-8

import argparse
import os
import platform
import sys
import six

import exts.archive
import exts.tmp
import re
from yatest.common import process


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
                segment_info = re.split(r'[/]', segment_info, maxsplit=1)[1]
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
    parser.add_argument("--coverage-tars", default=[], action='append')
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--source-root", required=True)
    parser.add_argument('--gotools-path', required=True)
    return parser.parse_args()


def get_host_platform():
    return platform.system().lower()


def build_report(params):
    tmpdir = 'tmp'
    mergedir = 'merge'
    result_dir = "go.coverage.report"
    cov_fn = 'cov'

    os.mkdir(mergedir)
    os.mkdir(result_dir)
    exts.fs.create_dirs(tmpdir)

    # TODO: We should switch from using GOPATH before go1.17
    os.environ["GOPATH"] = os.getcwd()
    os.environ["GO111MODULE"] = "auto"
    # add symlink to src_root because go html report builder searching go srcs in
    # GOPATH/src/(path to go srcs)
    os.symlink(params.source_root, os.path.join(os.environ["GOPATH"], "src"))

    for cov_tar in params.coverage_tars:
        exts.archive.extract_from_tar(cov_tar, tmpdir)

    cov_files = [os.path.join(tmpdir, fn) for fn in os.listdir(tmpdir)]
    merged_coverage_filename = combine_cov_files(cov_files, mergedir, cov_fn)
    cmd = [
        os.path.join(params.gotools_path, "pkg/tool/{}_amd64/cover".format(get_host_platform())),
        "-html=" + merged_coverage_filename,
        "-o",
        os.path.join(os.path.join(os.getcwd(), result_dir), "report.html"),
    ]

    os.environ["GOCACHE"] = os.path.abspath("tmp/.gocache")
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)

    exts.archive.create_tar(result_dir, params.output)


def main():
    args = get_options()
    build_report(args)


if __name__ == '__main__':
    main()
