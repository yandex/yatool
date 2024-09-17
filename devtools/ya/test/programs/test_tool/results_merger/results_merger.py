# coding=utf-8

"""
Merges results from multiple runs of one test
"""
import os
import json
import logging
import argparse
import collections
from datetime import datetime

from devtools.ya.test import common as test_common
from devtools.ya.test import reports
from devtools.ya.test.util import shared
import exts.archive
import exts.fs
import exts.tmp as yatemp
import exts.uniq_id
from devtools.ya.test import const
import devtools.ya.test.result
import devtools.ya.test.test_types.common as types_common

import six

logger = logging.getLogger(__name__)

FLAKY_DOC_URL = "https://docs.yandex-team.ru/ya-make/manual/tests/flaky"
MAX_FILE_SIZE = 10 * (1024**2)  # 10MiB


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-path", help="Project path")
    parser.add_argument("--suite-name", help="Test suite name")
    parser.add_argument("--output", dest="outputs", help="Test suite output", default=[], action="append")
    parser.add_argument("--target-platform-descriptor", dest="target_platform_descriptor")
    parser.add_argument(
        "--multi-target-platform-run", dest="multi_target_platform_run", action='store_true', default=False
    )
    parser.add_argument("--remove-tos", dest="remove_tos", action='store_true', default=False)
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
    parser.add_argument("--uid", action='store', default=None)
    parser.add_argument("--keep-temps", action='store_true', default=False)
    return parser.parse_args()


def merge_meta_files(files, dst, uid):
    logger.debug("Merging %d meta files", len(files))

    timestamp_format = "%Y-%m-%d %H:%M:%S.%f"
    all_metas = []
    for meta_path in files:
        with open(meta_path) as f:
            all_metas.append(json.load(f))

    # some sanity checks
    for field in ["project", "name"]:
        assert len(set(meta[field] for meta in all_metas)) == 1, (all_metas, field)

    def max_from_metas(field):
        return max([meta[field] for meta in all_metas])

    def min_from_metas(field):
        return min([meta[field] for meta in all_metas])

    start_time = min_from_metas("start_time")
    end_time = max_from_metas("end_time")
    # meta with most actual info
    latest_meta = sorted(all_metas, key=lambda x: x.get("start_time", ""))[-1]
    merged = {
        "uid": uid,
        "project": latest_meta["project"],
        "cwd": os.getcwd(),
        "end_time": end_time,
        "name": latest_meta["name"],
        "env_build_root": None,
        "test_timeout": latest_meta["test_timeout"],
        "start_time": start_time,
        "exit_code": max_from_metas("exit_code"),
        "elapsed": (
            datetime.strptime(end_time, timestamp_format) - datetime.strptime(start_time, timestamp_format)
        ).total_seconds(),
        "test_size": latest_meta["test_size"],
        "test_tags": latest_meta["test_tags"],
        "test_type": latest_meta["test_type"],
        "test_ci_type": latest_meta["test_ci_type"],
        "target_platform_descriptor": latest_meta["target_platform_descriptor"],
        "multi_target_platform_run": latest_meta["multi_target_platform_run"],
    }
    with open(dst, "w") as dst:
        json.dump(merged, dst, indent=4, sort_keys=True)

    return merged


def get_test_id(t):
    return "{}/{}".format(str(t.path), t.name)  # str is needed to convert None to str


def merge_tests(test1, test2):
    assert test1 == test2, "{} != {}".format(repr(test1), repr(test2))
    if test1.status == test2.status == const.Status.GOOD:
        return test1

    if test1.status == test2.status:
        return test1

    statuses = {test1.status, test2.status}

    # NOT_LAUNCHED + any = any
    if const.Status.NOT_LAUNCHED in statuses:
        return test1 if test1.status != const.Status.NOT_LAUNCHED else test2
    # TIMEOUT + GOOD = TIMEOUT
    # TIMEOUT + any = FLAKY
    elif const.Status.TIMEOUT in statuses:
        if const.Status.GOOD in statuses:
            return test1 if test1.status == const.Status.TIMEOUT else test2

    failed = test1 if abs(test1.status) > abs(test2.status) else test2
    # INTERNAL + any = INTERNAL
    # any + any other = FLAKY
    if failed.status != const.Status.INTERNAL:
        failed.status = const.Status.FLAKY
    return failed


class MergePlan(object):
    def __init__(self, nretries):
        self.nretries = nretries
        self.counter = collections.defaultdict(int)
        self.test_chunks = collections.defaultdict(set)
        self.not_launched_test_chunks = collections.defaultdict(set)
        self.test_map = collections.OrderedDict()
        self.chunks = {}

    def merge_chunks(self, chunks):
        for c in chunks[1:]:
            assert str(chunks[0]) == str(c), (chunks[0], c)

        chunk = select_worst_container(chunks)
        chunk.logs = merge_logs(chunks)
        self.chunks[chunks[0].get_name()] = chunk

        test_collection = collections.defaultdict(list)
        for chunk in chunks:
            for test_case in chunk.tests:
                test_id = get_test_id(test_case)
                self.counter[test_id] += 1
                if test_case.status == const.Status.NOT_LAUNCHED:
                    self.not_launched_test_chunks[test_id].add(chunk.get_name())
                else:
                    self.test_chunks[test_id].add(chunk.get_name())

                test_collection[test_id].append(test_case)

                if get_test_id(test_case) in self.test_map:
                    self.test_map[test_id] = merge_tests(test_case, self.test_map[test_id])
                else:
                    self.test_map[test_id] = test_case

        for test_id, tests in test_collection.items():
            for test_case in tests:
                if get_test_id(test_case) in self.test_map:
                    self.test_map[test_id] = merge_tests(test_case, self.test_map[test_id])
                else:
                    self.test_map[test_id] = test_case

            # Populate selected test case with logsdir
            for run_id, test_case in enumerate(tests, start=1):
                logsdir = test_case.logs.get("logsdir")
                if logsdir:
                    self.test_map[test_id].logs["logsdir_run{}".format(run_id)] = logsdir

    def set_flakiness(self):
        # Test if FLAKY if it isn't present in all chunks
        for test_id, test_case in self.test_map.items():
            if len(self.test_chunks.get(test_id, [])) > 1:
                test_case.status = const.Status.FLAKY
                test_case.comment = (
                    "Test was found in different chunks ({chunks}) and was marked as flaky\n"
                    "For more info see [[path]]{doc_url}[[rst]]\n"
                    "{comment}".format(
                        chunks=self.test_chunks[test_id],
                        comment=test_case.comment,
                        doc_url=FLAKY_DOC_URL,
                    )
                )
                continue

            missing_amount = self.nretries - self.counter[test_id]
            if not missing_amount:
                continue

            if test_case.status == const.Status.INTERNAL:
                continue

            test_case.status = const.Status.FLAKY
            test_case.comment = (
                "Test was not found in {nmissing} run{plurality} and was marked as flaky\n"
                "For more info see [[path]]{doc_url}[[rst]]\n"
                "{comment}".format(
                    nmissing=missing_amount,
                    plurality='' if missing_amount == 1 else 's',
                    comment=test_case.comment,
                    doc_url=FLAKY_DOC_URL,
                )
            )

    def scatter_tests_across_chunks(self):
        for chunk in self.chunks.values():
            chunk.tests = []

        for test_id, test_case in self.test_map.items():
            # Test might be present in several chunks - for more info see set_flakiness
            if test_id in self.test_chunks:
                chunk_id = sorted(self.test_chunks[test_id])[0]
            else:
                # Test has status NOT_LAUNCHED in every chunk
                chunk_id = sorted(self.not_launched_test_chunks[test_id])[0]
            self.chunks[chunk_id].tests.append(test_case)


def select_worst_container(entries):
    # merge suite info
    status_priority = [
        const.Status.INTERNAL,
        const.Status.FAIL,
        const.Status.TIMEOUT,
        const.Status.FLAKY,
        const.Status.MISSING,
        const.Status.GOOD,
    ]
    # XXX think about better merge
    # Before we get ultimate suite merge solution - get all container info from 'worst' case,
    score_list = [(x, status_priority.index(x.get_status())) for x in entries]
    return min(score_list, key=lambda x: x[1])[0]


def merge_suite_errors(suites):
    errors_map = collections.OrderedDict()
    for i, suite in enumerate(suites, start=1):
        for status, comment in suite._errors:
            errors_map.setdefault((status, comment.strip()), []).append(i)

    res = []
    for (status, comment), ids in errors_map.items():
        if comment:
            if len(ids) == 1:
                res.append((status, "Run {}".format(ids[0])))
            else:
                mtype = 'Info' if status == const.Status.GOOD else 'Error'
                res.append((status, "{} from {} runs".format(mtype, ids)))
            res.append((status, comment))
    return res


def merge_logs(containers):
    logs_map = {}
    for i, s in enumerate(containers, start=1):
        for log in s.logs:
            logs_map['{}_run{}'.format(log, i)] = s.logs[log]

    # If there is in interesting container - safe links to it
    for x in containers:
        if x.get_errors():
            logs_map.update(x.logs)
            break
    else:
        for x in containers:
            if x.get_info():
                logs_map.update(x.logs)
                break

    return logs_map


def merge_suites(suites):
    chunks_map = collections.defaultdict(list)

    for suite in suites:
        for chunk in suite.chunks:
            chunks_map[chunk.get_name()].append(chunk)

    plan = MergePlan(len(suites))
    for chunks in chunks_map.values():
        plan.merge_chunks(chunks)

    plan.set_flakiness()
    plan.scatter_tests_across_chunks()

    suite = types_common.PerformedTestSuite()
    suite.chunks = plan.chunks.values()

    # Must be done after setting chunks with tests
    worst_case = select_worst_container(suites)
    suite.metrics = worst_case.metrics
    suite.logs = merge_logs(suites)
    suite._errors = merge_suite_errors(suites)
    return suite


def merge(
    source_root,
    build_root,
    project_path,
    test_suite_name,
    outputs,
    target_platform_descriptor,
    multi_target_platform_run,
    uid,
    keep_temps,
):
    result_path = test_common.get_test_suite_work_dir(
        build_root,
        project_path,
        test_suite_name,
        target_platform_descriptor=target_platform_descriptor,
        multi_target_platform_run=multi_target_platform_run,
    )
    exts.fs.create_dirs(result_path)
    exts.fs.create_dirs(os.path.join(result_path, const.TESTING_OUT_DIR_NAME))

    replacements = [
        (source_root, "$(BUILD_ROOT)"),
        (build_root, "$(SOURCE_ROOT)"),
    ]
    resolver = reports.TextTransformer(replacements)
    suites = []

    logger.debug("Extracting results for %d outputs", len(outputs))
    for output in outputs:
        result = devtools.ya.test.result.TestPackedResultView(output)
        suite = devtools.ya.test.result.load_suite_from_result(result, output, resolver)
        suites.append(suite)

    # TODO
    # generate_merge_reports(suites, opts)

    logger.debug("Merging suites")
    suite = merge_suites(suites)
    suite.set_work_dir(result_path)

    # TODO add logs to files from generate_merge_reports

    trace_report = os.path.join(result_path, const.TRACE_FILE_NAME)
    suite.generate_trace_file(trace_report)

    def dump_header_separator(filename, dstfile):
        dstfile.write(b"%s\n" % (b"#" * 80))
        dstfile.write(b"## %s\n" % (six.ensure_binary(os.path.split(os.path.dirname(filename))[-1])))

    merge_meta_files(
        [os.path.join(output, 'meta.json') for output in outputs],
        os.path.join(result_path, 'meta.json'),
        uid,
    )
    shared.concatenate_files(
        [os.path.join(output, 'run_test.log') for output in outputs],
        os.path.join(result_path, 'run_test.log'),
        max_file_size=0,
        before_callback=dump_header_separator,
    )

    archive_postprocess = None if keep_temps else shared.archive_postprocess_unlink_files

    potential_outs = [
        ('allure.tar', False),
        ('coverage.tar', True),
        ('go.coverage.tar', True),
        ('java.coverage.tar', True),
        ('py2.coverage.tar', True),
        ('py3.coverage.tar', True),
        ('ts.coverage.tar', True),
        ('results_accumulator.log', True),
        ('unified.coverage.tar', True),
        ('yt_run_test.tar', True),
    ]
    for filename, avoid_collision in potential_outs:
        basket = [os.path.join(o, filename) for o in outputs if os.path.exists(os.path.join(o, filename))]
        if not basket:
            continue
        logger.debug("Merging %s", filename)
        out_path = os.path.join(result_path, filename)

        if filename.endswith(".tar"):
            merge_archives(basket, out_path, archive_postprocess, avoid_collision)
        else:
            shared.concatenate_files(basket, out_path, MAX_FILE_SIZE, before_callback=dump_header_separator)

    logger.debug("All done")


def merge_archives(archives, output, archive_postprocess, avoid_collision):
    logger.debug("Merging archives to %s", output)
    with yatemp.temp_dir() as tempdir:
        resultdir = os.path.join(tempdir, "coverage")
        os.makedirs(resultdir)

        for count, archive in enumerate(archives):
            dirname = os.path.join(tempdir, str(count))
            try:
                exts.archive.extract_from_tar(archive, dirname)
            except Exception as e:
                logger.exception("Failed to extract %s: %s", archive, e)
                continue
            else:
                for filename in os.listdir(dirname):
                    if avoid_collision:
                        base, ext = os.path.splitext(filename)
                        dst_name = "{}_{}{}".format(base, count, ext)
                    else:
                        dst_name = filename
                    os.rename(os.path.join(dirname, filename), os.path.join(resultdir, dst_name))

        exts.archive.create_tar(resultdir, output, postprocess=archive_postprocess)


def main():
    args = get_options()
    shared.setup_logging(args.log_level, args.log_path)
    logger.debug("Running result merging node for %s outputs", args.outputs)
    merge(
        args.source_root,
        os.getcwd(),
        args.project_path,
        args.suite_name,
        args.outputs,
        args.target_platform_descriptor,
        args.multi_target_platform_run,
        args.uid,
        args.keep_temps,
    )


if __name__ == '__main__':
    main()
