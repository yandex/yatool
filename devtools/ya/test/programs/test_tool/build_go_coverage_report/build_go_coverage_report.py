# coding: utf-8

import argparse
import json
import os
import sys
from collections.abc import Iterable

import exts.archive
from yatest.common import process
from library.python.testing import coverage_utils as coverage_utils_library
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output")
    parser.add_argument("--coverage-tars", default=[], action='append')
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--source-root", required=True)
    parser.add_argument('--gotools-path', required=True)
    parser.add_argument("--exclude-regexp")
    parser.add_argument("--prefix-filter")
    return parser.parse_args()


def build_summary(blocks: Iterable[lib_coverage.go_covdata.Block]) -> dict:
    """Statement coverage per file and in total.

    A source shared by several test modules is instrumented in each of them and 'covdata textfmt' emits its blocks
    once per package, so equal blocks are merged by coordinates with OR of the covered flag, as the html report does.
    """
    file_blocks: dict[str, lib_coverage.summary.GoBlocks] = {}
    for block in blocks:
        lib_coverage.summary.add_go_blocks(
            file_blocks.setdefault(block.filename, {}), [block.segment], [block.num_statements]
        )
    files = {name: lib_coverage.summary.go_statement_summary(file_blocks[name]) for name in sorted(file_blocks)}
    covered = sum(summary["statements"]["covered"] for summary in files.values())
    count = sum(summary["statements"]["count"] for summary in files.values())
    return {"total": lib_coverage.summary.statement_summary(covered, count), "files": files}


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

    mergedir = 'merge'
    os.mkdir(mergedir)
    tar_dirs = [os.path.join(tmpdir, str(index)) for index in range(len(args.coverage_tars))]
    for cov_tar, tar_dir in zip(args.coverage_tars, tar_dirs):
        exts.archive.extract_from_tar(cov_tar, tar_dir)
    # one text profile over all suites: counters are merged by covdata itself
    covdata_dir = lib_coverage.go_covdata.collect_coverage_data(tar_dirs, os.path.join(mergedir, 'covdata'))
    raw_profile = os.path.join(mergedir, 'raw')
    lib_coverage.go_covdata.write_text_profile(args.gotools_path, covdata_dir, raw_profile)
    file_filter = coverage_utils_library.make_filter(args.prefix_filter, args.exclude_regexp)
    # 'cover -html' looks sources up under GOPATH/src, so file paths are module-relative
    merged_coverage_file = os.path.join(mergedir, 'cov')
    with open(raw_profile) as profile, open(merged_coverage_file, 'w') as afile:
        afile.writelines(line + "\n" for line in lib_coverage.go_covdata.filter_profile(profile, file_filter))

    result_dir = "go.coverage.report"
    os.mkdir(result_dir)
    cmd = [
        lib_coverage.go_covdata.resolve_go_tool(args.gotools_path, "cover"),
        "-html=" + merged_coverage_file,
        "-o",
        os.path.join(cwd, result_dir, "report.html"),
    ]
    process.execute(cmd, stderr=sys.stderr, check_sanitizer=False)

    # machine-readable counterpart of the html report
    with open(raw_profile) as profile, open(os.path.join(result_dir, "summary.json"), "w") as afile:
        json.dump(build_summary(lib_coverage.go_covdata.parse_profile(profile, file_filter)), afile, indent=2)

    exts.archive.create_tar(result_dir, args.output)


def main():
    args = get_options()
    build_report(args)


if __name__ == '__main__':
    main()
