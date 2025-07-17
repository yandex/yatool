# coding=utf-8

"""
Accumulates output results from splitted tests
"""
import os
import sys
import six
import json
import shutil
import logging
import argparse
import collections
from datetime import datetime

import exts.archive
import exts.fs
import exts.uniq_id
import exts.tmp as yatemp
import devtools.ya.test.const
from devtools.ya.test.util import shared
from devtools.ya.test.programs.test_tool.lib import coverage
import library.python.cores as cores
from devtools.ya.test.test_types import common
from devtools.ya.test import const

logger = logging.getLogger(__name__)

MAX_FILE_SIZE = 10 * (1024**2)  # 10MiB
MAX_SUITE_CHUNKS = 3


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--accumulator-path", help="Node output directory")
    parser.add_argument("--output", dest="outputs", help="Test suite output", default=[], action="append")
    parser.add_argument("--log-path", dest="log_path", help="Log file path")
    parser.add_argument("--source-root", help="Source root", default="")
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument(
        "--concatenate-binaries",
        dest="concatenate_binaries",
        help="Input files that should be combined",
        default=[],
        action="append",
    )
    parser.add_argument("--keep-paths", help="Don't replace paths in trace files", default=False, action="store_true")
    parser.add_argument("--skip-file", dest='skip_files', help="Skip specified files", default=[], action="append")
    parser.add_argument("--skip-dir", dest='skip_dirs', help="Skip specified dirs", default=[], action="append")
    parser.add_argument("--truncate", help="Truncate test output's data", default=False, action="store_true")
    parser.add_argument("--fast-clang-coverage-merge", help="Use fast cov merge and specify path to the log file")
    parser.add_argument("--gdb-path")
    parser.add_argument("--keep-temps", default=False, action="store_true")
    parser.add_argument("--tests-limit-in-suite", action='store', type=int, default=0)

    return parser.parse_args()


@shared.timeit
def merge_dirs(args, dirs, dst, truncate, skip_files=None, skip_dirs=None, level=0):
    logger.debug("Merging content to '%s'", dst)
    files_map = get_output_files(dirs, set(skip_files or []), set(skip_dirs or []))
    for rel_filename, files in six.iteritems(files_map):
        logger.debug("Merging '%s' presented in: %s entries", rel_filename, len(files))
        merge_files(args, rel_filename, files, os.path.join(dst, rel_filename), truncate, level=level + 1)

    if not getattr(args, 'keep_paths', False) or 'ytest.report.trace' not in files_map:
        return

    for rel_filename, files in six.iteritems(files_map):
        if rel_filename != 'run_test.log':
            continue

        merge_in_trace(os.path.join(dst, 'ytest.report.trace'), files, os.path.join(dst, rel_filename))


@shared.timeit
def merge_files(args, filename, files, dst, truncate, level=0):
    dstdir = os.path.dirname(dst)
    exts.fs.create_dirs(dstdir)

    base_filename = os.path.basename(filename)
    filetype = base_filename.split(".")[-1]
    # Direct node outputs has level == 1 (don't try to merge files with special names in test's output)
    if level == 1 and base_filename == "meta.json":
        merge_meta_jsons(args, files, dst)
    elif level == 1 and filename == "ytest.report.trace":
        concatenate_traces(args, files, dst)
    elif level == 1 and base_filename == 'coverage.tar' and args.fast_clang_coverage_merge:
        merge_clang_coverage_archives_using_vfs(args, files, dst, logfile=args.fast_clang_coverage_merge)
    elif level == 1 and base_filename == "coverage_merge.log":
        merge_coverage_merge_logs(files, dst)
    elif args and base_filename in getattr(args, 'concatenate_binaries', []):
        concatenate_binaries(files, dst)
    elif filetype in ('tar', 'tgz') or base_filename.endswith('.tar.gz') or base_filename.endswith('.tar.zstd'):
        merge_archives(args, files, dst, truncate, level=level)
    elif filetype == 'sancov':
        coverage.merge.merge_raw_sancov(files, dst)
    elif base_filename.endswith('clang.profraw'):
        merge_clang_raw_profiles(files, dst)
    elif base_filename == 'run_test.log':
        shared.concatenate_files(files, dst, MAX_FILE_SIZE, before_callback=dump_header_separator)
    else:
        # remove method after DEVTOOLS-3250
        shared.concatenate_files(files, dst, MAX_FILE_SIZE if truncate else 0)


def dump_header_separator(filename, dstfile):
    nice_tail = b"#" * 80 + b"\n"
    dstfile.write(nice_tail)


def merge_coverage_merge_logs(files, dst):
    max_files = 10
    if len(files) > max_files:
        stat = [(filename, os.stat(filename).st_size) for filename in files]
        files = [filename for filename, _ in sorted(stat, key=lambda x: x[1])]
        # make sure there will be the smallest and the largest logs
        max_files -= 2
        step = int(len(files) // max_files)
        files = [files[0]] + files[1:-1:step][:max_files] + [files[-1]]

    shared.concatenate_files(files, dst)


@shared.timeit
def merge_archives(args, archives, dst, truncate, level=0):
    archive_postprocess = None if args.keep_temps else shared.archive_postprocess_unlink_files

    with yatemp.temp_dir() as tempdir:
        target_dirs = []
        for filename in archives:
            dirname = os.path.join(tempdir, exts.uniq_id.gen8())
            target_dirs.append(dirname)
            try:
                exts.archive.extract_from_tar(filename, dirname)
            except Exception:
                logger.exception("Exception during merge of %s", archives)
                for i, a in enumerate(archives):
                    exts.fs.move(a, dst + ".{}".format(i))
                return

        resultdir = os.path.join(tempdir, exts.uniq_id.gen8())
        exts.fs.create_dirs(resultdir)
        merge_dirs(args, target_dirs, resultdir, truncate, level=level)

        compression_filter = exts.archive.get_archive_filter_name(archives[0])
        logger.debug("Archive %s using %s compression filter", os.path.basename(dst), compression_filter)
        exts.archive.create_tar(
            resultdir, dst, compression_filter, exts.archive.Compression.Fast, postprocess=archive_postprocess
        )


@shared.timeit
def merge_clang_coverage_archives_using_vfs(args, archives, dst, logfile):
    import signal
    from devtools.ya.test.programs.test_tool import cov_merge_vfs

    procs = []

    def extract(files, dirname):
        for filename in files:
            exts.archive.extract_from_tar(filename, dirname)

    def spawn_extractor(*args):
        pid = os.fork()
        if pid == 0:
            try:
                extract(*args)
            finally:
                os._exit(0)
        else:
            procs.append(pid)

    def kill_all():
        while procs:
            pid = procs.pop()
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)

    with yatemp.temp_dir() as tempdir:
        with cov_merge_vfs.with_vfs(tempdir, logfile, auto_unmount_timeout=0) as vfs:
            # We can't completely utilize vfs extracting files sequentially - run in three processes
            # because we can't require more than 4 cpu from distbuild
            nchunks = 3
            for chunk in [archives[i::nchunks] for i in range(nchunks)]:
                spawn_extractor(chunk, tempdir)

            try:
                while procs:
                    try:
                        assert os.waitpid(procs.pop(), 0)[1] == 0
                    except AssertionError:
                        kill_all()
                        raise

                exts.archive.create_tar(tempdir, dst)
            except Exception:
                sys.stderr.write("Cov merge VFS log ({}):\n".format(logfile))
                with open(logfile) as afile:
                    for line in afile:
                        sys.stderr.write(line)

                # XXX (prettyboy): remove after fixes
                core_path = cores.recover_core_dump_file(sys.executable, os.getcwd(), vfs.get_pid())
                if args.gdb_path and core_path:
                    sys.stderr.write(cores.get_gdb_full_backtrace(sys.executable, core_path, args.gdb_path))

                raise


@shared.timeit
def merge_meta_jsons(args, files, dst):
    known_fields = [
        "cwd",
        "elapsed",
        "end_time",
        "env_build_root",
        "exit_code",
        "name",
        "project",
        "start_time",
        "test_timeout",
        "test_size",
        "test_tags",
        "target_platform_descriptor",
        "test_type",
        "test_ci_type",
        "multi_target_platform_run",
    ]
    start_time = datetime.max
    end_time = datetime.min

    result = {
        "cwd": os.getcwd(),
        "env_build_root": os.path.join(os.getcwd(), "environment", "build"),
    }
    exit_codes = []

    for filename in files:
        with open(filename) as file:
            file_content = file.read()
            if devtools.ya.test.const.NO_LISTED_TESTS in file_content:
                logger.debug("no tests were run")
                continue
            data = json.loads(file_content)
        unknown_fields = set(data.keys()) - set(known_fields)
        if unknown_fields:
            raise Exception(
                "Cannot merge meta.json correctly - it contains unknown fields '{}'".format(", ".join(unknown_fields))
            )

        result["project"] = data["project"]
        result["test_timeout"] = data["test_timeout"]
        result["name"] = data["name"]
        result["test_size"] = data["test_size"]
        result["test_tags"] = data["test_tags"]
        result["test_type"] = data["test_type"]
        result["test_ci_type"] = data["test_ci_type"]
        result["target_platform_descriptor"] = data["target_platform_descriptor"]
        # XXX after ya release replace with: result["multi_target_platform_run"] = data["multi_target_platform_run"]
        multi_target_platform_run = data.get("multi_target_platform_run")
        if multi_target_platform_run is not None:
            result["multi_target_platform_run"] = multi_target_platform_run

        exit_codes.append(data["exit_code"])
        logger.debug("Split test '%s' has ended with exit code %d", filename, data["exit_code"])
        start_time = min(start_time, datetime.strptime(data["start_time"], devtools.ya.test.const.TIMESTAMP_FORMAT))
        end_time = max(end_time, datetime.strptime(data["end_time"], devtools.ya.test.const.TIMESTAMP_FORMAT))

    result["start_time"] = start_time.strftime(devtools.ya.test.const.TIMESTAMP_FORMAT)
    result["end_time"] = end_time.strftime(devtools.ya.test.const.TIMESTAMP_FORMAT)
    result["elapsed"] = (end_time - start_time).total_seconds()
    # consider that rc may be negative
    finalrc = sorted(exit_codes, key=abs)[-1]
    logger.debug("Common exit code is set to %d", finalrc)
    result["exit_code"] = finalrc

    with open(dst, "w") as file:
        json.dump(result, file, indent=4, sort_keys=True)


def _cut_common_prefix(chunk_ids):
    def longest_common_prefix_len(arrays):
        prefix_length = 0
        if not arrays:
            return 0
        start_array = next(iter(arrays))
        for i in range(len(start_array)):
            if all([x[i] == start_array[i] for x in arrays]):
                prefix_length = i + 1
            else:
                break
        return prefix_length

    prefix_len = longest_common_prefix_len(chunk_ids.values())
    for chunk in chunk_ids:
        chunk_ids[chunk] = chunk_ids[chunk][prefix_len:]


@shared.timeit
def concatenate_traces(args, files, dst):
    # We need to replace after the concatenation all paths in the trace file that point to
    # modulo** dirs to ones that point to the merged dir
    build_root = os.path.realpath(os.getcwd())
    shared.concatenate_files(files, dst)
    trace_content = exts.fs.read_file_unicode(dst, binary=False)
    dst_build_rel_path = os.path.relpath(os.path.realpath(args.accumulator_path), build_root)
    chunk_ids = {}

    for o in args.outputs:
        o = os.path.realpath(o)
        output_build_rel_path = os.path.relpath(o, build_root)
        if args.keep_paths:
            relative = os.path.relpath(output_build_rel_path, dst_build_rel_path).split('/')
            assert relative[0] != os.pardir
            chunk_ids[o] = relative
        else:
            trace_content = trace_content.replace(output_build_rel_path + os.path.sep, dst_build_rel_path + os.path.sep)

    if not args.keep_paths:
        with open(dst, "w") as dstfile:
            dstfile.write(trace_content)
        return

    _cut_common_prefix(chunk_ids)

    statuses = dict()
    errors = []

    for chunk in chunk_ids:
        trace = os.path.join(chunk, "ytest.report.trace")
        assert os.path.isfile(trace)

        suite = common.PerformedTestSuite(None, None)
        suite.set_work_dir(os.getcwd())
        suite.load_run_results(trace)

        chunk_suffix = '_'.join(chunk_ids[chunk])
        chunk_suffix = '_' + chunk_suffix if chunk_suffix else chunk_suffix
        suite.chunk_suffix = chunk_suffix
        suite.chunk_name = ' '.join(chunk_ids[chunk])
        status = suite.get_status()

        if status not in statuses and status != const.Status.GOOD:
            statuses[status] = suite

    chunk_logs = {}
    invalid_logs = set()
    keylist = sorted(statuses.keys())
    for st in keylist[:MAX_SUITE_CHUNKS]:
        suite = statuses[st]
        chunk_suffix = suite.chunk_suffix
        for log in suite.logs.keys():
            if log == 'log':  # merged by results accumulator
                continue
            invalid_logs.add(log)
            chunk_logs[log + chunk_suffix] = suite.logs[log]

    suite = common.PerformedTestSuite(None, None)
    suite.set_work_dir(os.getcwd())
    suite.load_run_results(dst)
    for log in invalid_logs:
        del suite.logs[log]
    suite.logs.update(chunk_logs)
    suite._errors.extend(errors)

    if args.tests_limit_in_suite:
        # Rough barrier to prevent flooding CI with generated test cases
        # For more info see DEVTOOLSSUPPORT-55650
        shared.limit_tests(suite, limit=args.tests_limit_in_suite)

    os.remove(dst)  # rewrite trace to delete renamed
    shared.dump_trace_file(suite, dst)


@shared.timeit
def merge_in_trace(trace, files, dst):
    build_root = os.path.realpath(os.getcwd())
    trace_content = exts.fs.read_file_unicode(trace, binary=False)
    dst_rel_path = os.path.relpath(os.path.realpath(dst), build_root)

    for o in files:
        o = os.path.realpath(o)
        file_rel_path = os.path.relpath(o, build_root)
        trace_content = trace_content.replace(file_rel_path, dst_rel_path)

    with open(trace, "w") as dstfile:
        dstfile.write(trace_content)


@shared.timeit
def concatenate_binaries(files, dst):
    with open(dst, "wb") as dstfile:
        for fname in files:
            with open(fname, 'rb') as afile:
                shutil.copyfileobj(afile, dstfile)


@shared.timeit
def get_output_files(dirs, skip_files, skip_dirs):
    files_map = collections.defaultdict(list)
    dirs_set = {os.path.normpath(d) for d in dirs}
    for dirname in dirs:
        top_level = True
        for root, _, files in os.walk(dirname):
            if not top_level and os.path.normpath(root) in dirs_set:
                # NOTE:(DEVTOOLSSUPPORT-66303) Skip nested output dirs
                continue
            if os.path.basename(root) in skip_dirs:
                logger.debug("Skipped dir: %s", root)
                continue
            for filename in files:
                abs_name = os.path.join(root, filename)
                if filename in skip_files:
                    logger.debug("Skipped file: %s", abs_name)
                    continue
                rel_name = os.path.relpath(abs_name, dirname)
                files_map[rel_name].append(abs_name)
            top_level = False
    return files_map


@shared.timeit
def merge_clang_raw_profiles(files, dst):
    suffix = "clang.profraw"
    dst_dir, dst_name = os.path.split(dst)
    prefix = dst_name[: -len(suffix)]
    for counter, filename in enumerate(files):
        target = os.path.join(dst_dir, "".join([prefix, str(counter), suffix]))
        exts.fs.copy_file(filename, target)


def merge_test_outputs(args):
    if args.skip_files:
        logger.debug("Going to skip top level files: %s", args.skip_files)
    merge_dirs(args, args.outputs, args.accumulator_path, args.truncate, args.skip_files, args.skip_dirs)


def main():
    args = get_options()
    shared.setup_logging(args.log_level, args.log_path, fmt="%(asctime)-15s %(levelname)-5s: %(message)s")

    # create node's output file
    open(args.log_path, 'w').close()
    if args.fast_clang_coverage_merge:
        open(args.fast_clang_coverage_merge, 'w').close()
    shared.dump_dir_tree()
    logger.debug("Running result accumulator node for %s outputs", args.outputs)
    merge_test_outputs(args)
    logger.debug('Time spend: %s', json.dumps(shared.timeit.stat, sort_keys=True))


if __name__ == '__main__':
    main()
