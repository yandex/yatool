# coding: utf-8

import os
import bisect
import logging
import argparse
import itertools
import six

import coverage
import coverage.report
import coverage.results
import ujson as json

import exts.fs
import exts.func
import exts.archive
from devtools.ya.test.programs.test_tool.lib import runtime
from test.util import shared
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage

logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--coverage-path', required=True)
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--binary', dest='binaries', action='append', default=[], required=True)
    parser.add_argument('--log-path')
    parser.add_argument('--log-level', default='INFO')

    args = parser.parse_args()
    return args


class CoverageReporter(object):
    def __init__(self, covobj):
        self.coverage = covobj
        self.config = self.coverage.config
        self.branches = covobj.get_data().has_arcs()
        self.total = coverage.results.Numbers()
        self.report_data = {}

    def parse(self, ignore_errors=False):
        covdata = {}
        for reporter, analysis in coverage.report.get_analysis_to_report(self.coverage, None):
            filename = analysis.filename
            missing = analysis.missing - {0}
            covered = analysis.executed
            if self.branches:
                missing_branches = analysis.missing_branch_arcs()
            else:
                missing_branches = {}

            # TODO add funcs declarations extraction to CythonModuleTracer
            if hasattr(reporter, 'parser'):
                funcs = reporter.parser.raw_funcdefs
            else:
                funcs = []

            assert filename not in covdata, "Impossibru - coverage data must be merged (%s)" % filename
            covdata[filename] = {
                'segments': get_segments(covered, missing, missing_branches),
                'functions': get_functions(funcs, covered),
            }

        return covdata


def lines_to_unified_segments(covdata, missing_branches):
    # default python coverage dumps line number for coverage - change it to index (starts from 0)
    for ln, state in covdata:
        # add empty segment to highlight missing branch
        # missing branch segment might be only before covered segment
        if ln in missing_branches and state:
            yield (ln - 1, 0, ln - 1, 0, 0)
        yield (ln - 1, 0, ln, 0, state)


def get_segments(covered, missing, missing_branches):
    covered = ((ln, 1) for ln in covered)
    missing = ((ln, 0) for ln in missing)
    covdata = sorted(itertools.chain(covered, missing))
    return lib_coverage.merge.compact_segments(lines_to_unified_segments(covdata, missing_branches))


def func_covered(covered, start, end):
    if not covered:
        return 0
    pos = bisect.bisect_left(covered, start + 1)
    return pos < len(covered) and covered[pos] <= end


def get_functions(funcs, covered):
    # returns functions coverage in unified format: dict(pos, dict(instantiation: counter))
    covered = sorted(covered)
    data = {}
    for start, end, name in funcs:
        data[(start - 1, 0)] = {name: int(func_covered(covered, start, end))}
    return data


def extract(filename):
    tmpdir = 'tmp'
    exts.fs.create_dirs(tmpdir)
    exts.archive.extract_from_tar(filename, tmpdir)
    return tmpdir


def get_data_files(arch_path):
    datadir = extract(arch_path)
    files = [os.path.join(datadir, filename) for filename in os.listdir(datadir)]
    return lib_coverage.util.get_python_profiles(files)


def setup_env(params):
    shared.setup_logging(params.log_level, params.log_path)
    # create node's output
    with open(params.output, "w") as afile:
        afile.write("no valid output was provided")

    assert os.path.exists(params.source_root)
    # we need to specify source root to build AST for coverage
    os.environ["PYTHON_COVERAGE_ARCADIA_SOURCE_ROOT"] = params.source_root


def load_coverage(filename):
    cov = coverage.Coverage(data_file=filename)
    cov.load()
    return cov


def resolve_coverage(data_file, output_file):
    covobj = load_coverage(data_file)

    reporter = CoverageReporter(covobj)
    data = reporter.parse()
    data = {fname: fdata for fname, fdata in six.iteritems(data) if fname != "__SKIP_FILENAME__"}
    lib_coverage.export.dump_coverage(data, output_file)


def main():
    args = parse_args()
    setup_env(args)

    data_files = get_data_files(args.coverage_path)
    # test might be skipped by filter and there will be no coverage data
    if not data_files:
        logger.debug('No profdata available')
        with open(args.output, 'w') as afile:
            json.dump({}, afile)
        return

    assert len(data_files) <= 1, "Found several coverage reports after merge: {}".format(data_files)

    source_dir = os.path.abspath('root')
    include_map_file = os.path.abspath('include_map.json')
    # Cython coverage plugin requires cpp generated files and cython's include map
    shared.extract_cython_entrails(args.binaries, source_dir, include_map_file)
    os.environ.update(
        {
            'PYTHON_COVERAGE_CYTHON_BUILD_ROOT': source_dir,
            'PYTHON_COVERAGE_CYTHON_INCLUDE_MAP': include_map_file,
        }
    )

    resolve_coverage(data_files[0], args.output)

    logger.debug('maxrss: %d', runtime.get_maxrss())
    return 0


if __name__ == '__main__':
    exit(main())
