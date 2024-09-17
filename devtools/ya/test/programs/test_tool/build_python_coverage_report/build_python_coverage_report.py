# coding: utf-8

import os
import sys
import argparse
import traceback

import coverage

import exts.archive
import exts.tmp
from devtools.ya.test.util import shared
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output")
    parser.add_argument("--coverage-path")
    parser.add_argument("--source-root")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--binary", dest='binaries', action='append', default=[])
    return parser.parse_args()


def build_report(params):
    tmpdir = os.path.abspath("tmp")
    exts.archive.extract_from_tar(params.coverage_path, tmpdir)

    result_dir = "python.coverage.report"
    os.mkdir(result_dir)

    files = os.listdir(tmpdir)
    files = lib_coverage.util.get_python_profiles(files)

    if not files:
        sys.stderr.write("No coverage data found. Will not generate html report for python coverage.")
        exts.archive.create_tar(result_dir, params.output)
        return

    assert len(files) == 1, "Coverage wasn't merged: %s" % files

    output = os.path.join(result_dir, "cov")
    os.rename(os.path.join(tmpdir, files[0]), output)

    # required python files are not built-in into current binary
    # and may be partly available with not actual content (test_tool is built not from trunk)
    # that's why we say to the coverage where plugin can find required sources
    os.environ["PYTHON_COVERAGE_ARCADIA_SOURCE_ROOT"] = params.source_root

    source_dir = os.path.abspath('root')
    include_map_file = os.path.abspath('include_map.json')
    # Cython coverage plugin requires cpp generated files and cython's include map
    shared.extract_cython_entrails(params.binaries, source_dir, include_map_file)
    os.environ.update(
        {
            'PYTHON_COVERAGE_CYTHON_BUILD_ROOT': source_dir,
            'PYTHON_COVERAGE_CYTHON_INCLUDE_MAP': include_map_file,
        }
    )

    debug_params = []
    if params.verbose:
        debug_params = ['pid', 'trace', 'sys', 'config']

    cov = coverage.Coverage(data_file=output, debug=debug_params)
    cov.load()
    if params.verbose:
        sys.stderr.write("Measured_files: {}\n".format(u", ".join(cov.data.measured_files()).encode('utf-8')))

    try:
        # pretty too slow, maybe we don't need it
        cov.report(file=sys.stderr)
        cov.html_report(directory=result_dir)
        cov.json_report(outfile=os.path.join(result_dir, "cov.json"))
    except coverage.misc.CoverageException:
        sys.stderr.write("Python coverage: {}".format(traceback.print_exc()))

    exts.archive.create_tar(result_dir, params.output)


def main():
    args = get_options()
    build_report(args)


if __name__ == '__main__':
    main()
