# coding: utf-8

import argparse
import os
import sys

import exts.archive
from yatest.common import process
from library.python.testing import coverage_utils


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output")
    parser.add_argument("--coverage-path", default=[], dest="coverage_paths", action='append')
    parser.add_argument('--gotools-path', required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--filename-prefix")
    parser.add_argument("--exclude-regexp")
    parser.add_argument("--prefix-filter")
    return parser.parse_args()


def extract_filename(segment_info):
    return segment_info.split(":")[0]


def resolve_go_tool(path, binname):
    path = os.path.join(path, "pkg/tool")
    names = os.listdir(path)
    # go toolchain contains only one platform
    assert len(names) == 1, names
    return os.path.join(path, names[0], binname)


def merge_coverage(params):
    cov_dirs = []
    for coverage_path in params.coverage_paths:
        dirname = exts.uniq_id.gen32()
        assert not os.path.exists(dirname)
        exts.archive.extract_from_tar(coverage_path, dirname)
        cov_dirs.append(dirname)
    mergebin_dir = params.output + '.mergebin'
    os.mkdir(mergebin_dir)
    cmd = [
        resolve_go_tool(params.gotools_path, "covdata"),
        "merge",
        "-i",
        ','.join(cov_dirs),
        "-o",
        mergebin_dir,
    ]
    os.environ["GOCACHE"] = os.path.abspath("tmp/.gocache")
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)
    merge_dir = params.output + '.merge'
    os.mkdir(merge_dir)
    merged_covraw = os.path.join(merge_dir, 'covraw')
    merged_cov = os.path.join(merge_dir, 'cov')
    cmd = [
        resolve_go_tool(params.gotools_path, "covdata"),
        "textfmt",
        "-i",
        mergebin_dir,
        "-o",
        merged_covraw,
    ]
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)
    file_filter = (
        coverage_utils.make_filter(params.prefix_filter, params.exclude_regexp)
        if params.prefix_filter or params.exclude_regexp
        else None
    )
    filtered_coverage = {}
    with open(merged_covraw, 'r') as f:
        mode_line = f.readline()  # coverage mode line
        for line in f:
            line = line.strip().replace(params.source_root + '/', '')
            cover_flag = bool(int(line[-1]))
            segment_info = line[:-1]
            if not file_filter or file_filter(extract_filename(segment_info)):
                filtered_coverage[segment_info] = cover_flag | (
                    filtered_coverage[segment_info] if segment_info in filtered_coverage else False
                )
    os.unlink(merged_covraw)
    with open(merged_cov, 'w') as f:
        f.write(mode_line)
        for key, value in filtered_coverage.items():
            f.write(f"{params.filename_prefix if params.filename_prefix else ''}{key}{str(int(value))}\n")
    exts.archive.create_tar(merge_dir, params.output)


def main():
    args = get_options()
    merge_coverage(args)


if __name__ == '__main__':
    main()
