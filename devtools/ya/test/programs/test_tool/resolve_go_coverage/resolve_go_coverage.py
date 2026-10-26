import logging
import argparse
from collections.abc import Iterable

import exts.archive
from devtools.ya.test.programs.test_tool.lib import runtime
from devtools.ya.test.util import shared
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage
from library.python.testing import coverage_utils as coverage_utils_library

logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--coverage-path', required=True)
    parser.add_argument('--log-path')
    parser.add_argument('--log-level', default='INFO')
    parser.add_argument("--exclude-regexp")
    parser.add_argument("--prefix-filter")
    parser.add_argument("--gotools-path", required=True, help="go toolchain with 'covdata'")

    args = parser.parse_args()
    return args


def setup_env(params):
    shared.setup_logging(params.log_level, params.log_path)
    # create node's output
    with open(params.output, "w") as afile:
        afile.write("no valid output was provided")


def transform_coverage(blocks: Iterable[lib_coverage.go_covdata.Block]) -> dict[str, dict]:
    """Build unified coverage per file: segments, aligned statement weights and statement summary.

    'covdata textfmt' keeps blocks unique within a package only, so equal blocks of a source shared by several
    packages are merged by coordinates with OR of the covered flag, as the profile merge did before covdata.
    The go profile carries no function information, so "functions" is empty.
    """
    file_blocks: dict[str, lib_coverage.summary.GoBlocks] = {}
    for block in blocks:
        lib_coverage.summary.add_go_blocks(
            file_blocks.setdefault(block.filename, {}), [block.segment], [block.num_statements]
        )

    uni_cov = {}
    for filename, blocks_of_file in file_blocks.items():
        coords = sorted(blocks_of_file)  # by start line, then start pos
        uni_cov[filename] = {
            "segments": [[*coord, blocks_of_file[coord][1]] for coord in coords],
            "functions": {},
            "statements": [blocks_of_file[coord][0] for coord in coords],
            "summary": lib_coverage.summary.go_statement_summary(blocks_of_file),
        }
    return uni_cov


def main():
    args = parse_args()
    setup_env(args)

    exts.archive.extract_from_tar(args.coverage_path, 'tmp')
    covdata_dir = lib_coverage.go_covdata.collect_coverage_data(['tmp'], 'covdata')
    file_filter = coverage_utils_library.make_filter(args.prefix_filter, args.exclude_regexp)
    lib_coverage.go_covdata.write_text_profile(args.gotools_path, covdata_dir, 'merged.cov')
    with open('merged.cov') as profile:
        unified_cov = transform_coverage(lib_coverage.go_covdata.parse_profile(profile, file_filter))
    lib_coverage.export.dump_coverage(unified_cov, args.output)

    logger.debug('maxrss: %d', runtime.get_maxrss())
    return 0


if __name__ == "__main__":
    exit(main())
