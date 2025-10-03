# coding: utf-8

import argparse
import gc
import heapq
import json
import logging
import os
import re
import signal

import exts.fs
import exts.archive
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage
from devtools.common import libmagic
from devtools.ya.test.programs.test_tool.lib import runtime
from devtools.ya.test.util import shared
from devtools.ya.test import const
from yatest.common import process

import six

logger = logging.getLogger(__name__)
SANCOV_REGEXP = re.compile(r'(.*?)\.\d+?\.sancov')
SHUTDOWN_REQUESTED = [False]


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--llvm-profdata-tool', required=True)
    parser.add_argument('--llvm-cov-tool', required=True)
    parser.add_argument('--coverage-path', required=True)
    parser.add_argument('--source-root')
    parser.add_argument('--mcdc-coverage', action='store_true')
    parser.add_argument('--branch-coverage', action='store_true')
    parser.add_argument('--log-path')
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument('--target-binary')
    parser.add_argument('--timeout', default=0, type=int)
    parser.add_argument('--test-mode', action='store_true')

    args = parser.parse_args()
    return args


def graceful_shutdown(signo, frame):
    SHUTDOWN_REQUESTED[0] = True


def is_shutdown_requested():
    return SHUTDOWN_REQUESTED[0]


@shared.timeit
def merge_segments(s1, s2):
    return lib_coverage.merge.merge_clang_segments([s1, s2])


@shared.timeit
def merge_branches(b1, b2):
    return lib_coverage.merge.merge_clang_branches([b1, b2])


@shared.timeit
def saturate_coverage(covtype, covdata, source_root, cache, mcdc=False, branches=False):
    if covtype == 'files':
        filename = covdata['filename']
    elif covtype == 'functions':
        filename = covdata['filenames'][0]
    else:
        raise Exception('Unknown coverage type: {}'.format(covtype))

    if lib_coverage.util.should_skip(filename, source_root):
        return None

    relfilename = lib_coverage.util.normalize_path(filename, source_root)
    assert relfilename, (filename, source_root)

    if relfilename not in cache:
        cache[relfilename] = {
            'segments': [],
            'functions': {},
        }
        if branches:
            cache[relfilename]['branches'] = []

    cache_entry = cache[relfilename]

    if covtype == 'files':
        if 'summary' in covdata:
            if 'summary' in cache_entry:
                logger.warning(
                    f"Found summary report duplicate for '{relfilename}': cache_entry={cache_entry}, covdata={covdata}"
                )
            cache_entry['summary'] = covdata['summary']

        if mcdc and 'mcdc_records' in covdata:
            if 'mcdc' in cache_entry:
                raise AssertionError("Found MC/DC coverage duplicate")
            cache_entry['mcdc'] = covdata['mcdc_records']

        if branches:
            if not cache_entry['branches']:
                cache_entry['branches'] = covdata['branches']
            else:
                cache_entry['branches'] = merge_branches(cache_entry['branches'], covdata['branches'])

        if not cache_entry['segments']:
            cache_entry['segments'] = covdata['segments']
        else:
            cache_entry['segments'] = merge_segments(cache_entry['segments'], covdata['segments'])
    else:
        start_pos = covdata['regions'][0]
        key = (start_pos[0] - 1, start_pos[1] - 1)

        func_entry = cache_entry['functions']
        if key not in func_entry:
            func_entry[key] = []

        # Slice functions to avoid oversaturation of the template functions
        stack = func_entry[key]
        heapq.heappush(stack, (covdata['count'], covdata['name']))
        while len(stack) > const.COVERAGE_FUNCTION_ENTRIES_LIMIT:
            heapq.heappop(stack)


@shared.timeit
def extract(filename):
    tmpdir = 'tmp'
    exts.fs.create_dirs(tmpdir)
    exts.archive.extract_from_tar(filename, tmpdir)
    return tmpdir


@shared.timeit
def merge_covdata(profdata_tool, bin_name, filenames):
    assert filenames
    output = os.path.abspath(bin_name + ".profdata")
    cmd = [profdata_tool, 'merge', '--sparse', '-o', output] + filenames
    process.execute(cmd, check_sanitizer=False)
    return output


@shared.timeit
def load_block(data):
    return json.loads(data)


@shared.timeit
def is_generated_code(filename, source_root):
    return lib_coverage.util.should_skip(filename, source_root)


def gen_empty_output(filename):
    # Overwrite invalid output with empty one.
    # It's the case when test has DEPENDS on binary which was not launched in tests
    # (for example FORK_SUBTESTS() used and current chunk doesn't use requested binary)
    with open(filename, "w") as afile:
        afile.write("")


def normalize_function_entries(coverage):
    logger.debug("Normalize functions")
    for filename in coverage:
        filedata = coverage[filename]
        for pos, data in filedata.get('functions', {}).items():
            filedata['functions'][pos] = {k: v for v, k in data}


def main():
    # noinspection PyUnresolvedReferences
    import app_ctx

    args = parse_args()
    shared.setup_logging(args.log_level, args.log_path, fmt="%(asctime)-15s %(levelname)-5s: %(message)s")

    if args.timeout:
        timeout = args.timeout - 60
        logger.debug("Set alarm %d", timeout)
        signal.signal(signal.SIGALRM, graceful_shutdown)
        signal.alarm(timeout)

    # create node's output
    with open(args.output, "w") as afile:
        afile.write("no valid output was provided")

    datadir = extract(args.coverage_path)
    target_dir = 'profdata'
    exts.fs.create_dirs(target_dir)
    filenames = [os.path.join(datadir, filename) for filename in os.listdir(datadir)]
    logger.debug("Profdata files: %s", filenames)

    # test might be skipped by filter and there will be no coverage data
    if not filenames:
        logger.debug('No profdata available')
        with open(args.output, 'w') as afile:
            json.dump({}, afile)
        return

    cov_files_map = lib_coverage.util.get_coverage_profiles_map(filenames)
    logger.debug("Coverage profile files map: %s", cov_files_map)

    binname = os.path.basename(args.target_binary)
    if not cov_files_map.get(binname):
        logger.warning("No valid coverage profiles found for %s", binname)
        gen_empty_output(args.output)
        return 0
    if not libmagic.is_elf(six.ensure_binary(args.target_binary)):
        logger.warning("Skipping non-executable: %s", args.target_binary)
        gen_empty_output(args.output)
        return 0

    app_ctx.display.emit_status('Indexing profiles')
    profdata_path = merge_covdata(args.llvm_profdata_tool, binname, cov_files_map[binname])

    app_ctx.display.emit_status('Exporting profile (target:{})'.format(args.target_binary))
    # https://pg.at.yandex-team.ru/5908
    gc.disable()

    coverage = {}
    limit_of_suspicion = 100 * 1024**2

    logger.debug("Resolving coverage (source root: %s)", args.source_root)

    cmd = [
        args.llvm_cov_tool,
        'export',
        '-j',
        '4',
        '--instr-profile',
        profdata_path,
        '--object',
        args.target_binary,
    ]

    if args.mcdc_coverage:
        cmd.append('--show-mcdc-summary')

    cmd += lib_coverage.util.get_default_llvm_export_args()

    def process_block(covtype, filename, data):
        # XXX temporary hack till https://st.yandex-team.ru/DEVTOOLS-3757 is done
        # We need to skip such blocks to avoid useless job and memory exhaustion parsing data
        if len(data) > limit_of_suspicion:
            if is_generated_code(filename, args.source_root):
                return

        saturate_coverage(
            covtype,
            load_block(data),
            args.source_root,
            coverage,
            mcdc=args.mcdc_coverage,
            branches=args.branch_coverage,
        )

    lib_coverage.export.export_llvm_coverage(cmd, process_block, cancel_func=is_shutdown_requested)

    normalize_function_entries(coverage)
    lib_coverage.export.dump_coverage(coverage, args.output)

    logger.debug('Time spend: %s', json.dumps(shared.timeit.stat, sort_keys=True))
    logger.debug('maxrss: %d', runtime.get_maxrss())
    return os.environ.get("YA_COVERAGE_FAIL_RESOLVE_NODE", 0)


if __name__ == '__main__':
    exit(main())
