import collections
import copy
import logging
import os
import six

import cityhash
from devtools.ya.test import const as constants
import devtools.ya.test.common as test_common
from devtools.ya.test.const import Status
from exts.hashing import md5_value
import exts.yjson as json

import typing as tp

if tp.TYPE_CHECKING:
    import devtools.ya.test.test_types.common as tt_common  # noqa


logger = logging.getLogger(__name__)

MAX_TEST_LEN = 1000
HASH_LEN = 32

REPORT_ENTRY_COMPLETED_FIELDS = [
    'chunk',
    'chunk_hid',
    'duration',
    'error_type',
    'hid',
    'id',
    'links',
    'metrics',
    'name',
    'path',
    'result',
    'rich-snippet',
    'size',
    'status',
    'subtest_name',
    'suite',
    'suite_hid',
    'suite_status',
    'tags',
    'type',
    'uid',
]


def get_blank_record():
    return {
        "id": "",
        "error_type": "",
        "toolchain": "",
        "path": "",
        "type": "",
        "name": "",
        "subtest_name": "",
        "status": TestStatus.Good,
        "rich-snippet": "",
        "uid": "",
        "owners": {},
        "links": {},
        "metrics": {},
    }


class TestStatus(constants.Enum):
    Good = "OK"
    Fail = "FAILED"
    Skipped = "SKIPPED"
    Discovered = "DISCOVERED"
    NotLaunched = "NOT_LAUNCHED"


class ErrorType(constants.Enum):
    Regular = "REGULAR"
    Timeout = "TIMEOUT"
    BrokenDeps = "BROKEN_DEPS"
    Flaky = "FLAKY"
    ExpectedFailed = "XFAILED"
    UnexpectedPassed = "XPASSED"
    Internal = "INTERNAL"


def convert_test_status(status):
    assert isinstance(status, int), "Status code expected, not '{}'".format(status)
    if status in [Status.GOOD, Status.XFAIL, Status.XPASS]:
        return TestStatus.Good
    elif status in [Status.SKIPPED, Status.DESELECTED]:
        return TestStatus.Skipped
    elif status in [Status.NOT_LAUNCHED]:
        return TestStatus.NotLaunched
    return TestStatus.Fail


def get_test_error_type(status):
    assert isinstance(status, int), "Status code expected, not '{}'".format(status)
    if status in [Status.GOOD, Status.NOT_LAUNCHED, Status.SKIPPED]:
        return None
    return {
        Status.FLAKY: ErrorType.Flaky,
        Status.XFAIL: ErrorType.ExpectedFailed,
        Status.XPASS: ErrorType.UnexpectedPassed,
        Status.TIMEOUT: ErrorType.Timeout,
        Status.INTERNAL: ErrorType.Internal,
        Status.MISSING: ErrorType.BrokenDeps,
    }.get(status, ErrorType.Regular)


def get_hash_id(*args, **kwargs):
    """
    :return: 64bit hash
    """
    return get_id(*args, hash_func=cityhash.hash64, **kwargs)


def get_id(project_path, name="", subtest="", test_type=None, suite_id=None, chunk_name=None, hash_func=md5_value):
    """
    :return: md5 hexdigest
    """
    val = "{}-{}-{}".format(project_path, name, subtest)
    if test_type == "style":
        val += "-" + test_type
    if suite_id is not None:
        val += "-" + str(suite_id)
    if chunk_name is not None:
        val += "-" + str(chunk_name)
    return hash_func(six.ensure_binary(val))


def adapt_path(filename, suite_name, merger_out_dir):
    if merger_out_dir is None:
        return filename
    dirs = filename.split(os.path.sep)

    assert 'testing_out_stuff' in dirs or 'run_test.log' in dirs
    dirs_cutoff = dirs.index('testing_out_stuff' if 'testing_out_stuff' in dirs else 'run_test.log')
    new_dirs = [merger_out_dir, suite_name] + dirs[dirs_cutoff:]

    return os.path.join(*new_dirs)


def get_test_logs(logs, suite_name, merger_out_dir):
    if logs:
        res = {}
        for ttype, filename in logs.items():
            if not filename:
                continue

            link = adapt_path(filename, suite_name, merger_out_dir)
            if link:
                res[ttype] = [link]
        return res
    else:
        return {}


def build_container_comment(container):
    header = "{} test{}".format(len(container.tests), '' if len(container.tests) == 1 else 's')
    if container.tests:
        test_results = [test.status for test in container.tests]
        header += ': {}[[rst]]'.format(
            ", ".join(test_common.get_formatted_statuses(test_results.count, "[[{marker}]]{count} - {status}[[rst]]"))
        )
    return "\n\n".join([_f for _f in [header, container.get_comment()] if _f])


def get_suite_metrics(suite):
    metrics = copy.deepcopy(suite.metrics)
    metrics.update(
        {
            # XXX space if required to make this metrics first on the CI page
            " test_count": len(suite.tests),
        }
    )
    for redundant_metric in ["suite_finish_timestamp", "suite_start_timestamp"]:
        if redundant_metric in metrics:
            del metrics[redundant_metric]

    status_map = {status: 0 for status in TestStatus.enumerate() + ErrorType.enumerate()}
    # use status fail instead of regular to make it a little bit more intelligible
    del status_map[ErrorType.Regular]

    for test_case in suite.tests:
        status = convert_test_status(test_case.status)
        if status == TestStatus.Fail:
            status = get_test_error_type(test_case.status)
            if status == ErrorType.Regular:
                status = TestStatus.Fail
        status_map[status] += 1

    for status, val in status_map.items():
        metrics["{}_tests".format(status.lower())] = val
    return metrics


def truncate_test_name(class_name, subtest_name):
    hash_prefix = "-hash:"
    if len(class_name + '::' + subtest_name) > MAX_TEST_LEN:
        subtest_len = MAX_TEST_LEN - HASH_LEN - len(class_name + "::") - len(hash_prefix)
        test_name_tail = subtest_name[subtest_len:]
        test_name_tail_hash = md5_value(test_name_tail)
        return "{}{}{}".format(subtest_name[:subtest_len], hash_prefix, test_name_tail_hash)
    else:
        return subtest_name


def make_suites_results_prototype(suites, merger_out_dir=None):
    # type: (list[tt_common.AbstractTestSuite], tp.Any) -> list[dict]
    # XXX: move from global scope, just to fix checks
    from yatest_lib import external

    entries = []

    for suite in suites:
        # XXX: remove after release ya-bn with real uids
        if suite.is_skipped() and suite.uid is None:
            suite.uid = "skipped:{}".format(
                get_id(suite.project_path, suite.get_type(), test_type=suite.get_ci_type_name())
            )
        general_info = get_blank_record()
        general_info.update(
            {
                "type": suite.get_ci_type_name(),
                "path": suite.project_path,
                "tags": suite.tags,
                "uid": suite.uid,
                "target_platform_descriptor": suite.target_platform_descriptor,
                "is_skipped": suite.is_skipped(),
                "test_size": suite.test_size,
            }
        )

        if general_info["type"] == "test":
            general_info["size"] = suite.test_size

        suite_entry = copy.deepcopy(general_info)
        suite_id = get_id(suite.project_path, suite.get_type(), test_type=suite.get_ci_type_name())
        # WIP: https://st.yandex-team.ru/DEVTOOLS-8716
        suite_hid = get_hash_id(suite.project_path, suite.get_type(), test_type=suite.get_ci_type_name())
        suite_entry.update(
            {
                "id": suite_id,
                "hid": suite_hid,
                "suite": True,
                "name": suite.get_type(),
                "rich-snippet": build_container_comment(suite),
                "links": get_test_logs(suite.logs, suite.name, merger_out_dir),
                "metrics": get_suite_metrics(suite),
            }
        )
        # Relaxed status which doesn't depend on status of testcases.
        # For more info see https://st.yandex-team.ru/DEVTOOLS-8750
        relaxed_status = None
        if suite.get_status() == Status.SKIPPED and suite.is_skipped():
            status = TestStatus.Discovered
            error_type = None
        else:
            status = convert_test_status(suite.get_status())
            # looks like infrastructure error - may be test node failed
            if suite.get_status() == Status.MISSING and not suite.get_comment():
                error_type = ErrorType.Internal
                snippet = "Infrastructure error - contact devtools@ for details"
                suite_entry["rich-snippet"] = snippet
            else:
                relaxed_status = convert_test_status(suite.get_status(relaxed=True))
                error_type = get_test_error_type(suite.get_status())

        suite_entry["status"] = status
        suite_entry["suite_status"] = relaxed_status or status
        if error_type:
            suite_entry["error_type"] = error_type

        effective_suite_id = suite_id
        effective_suite_hid = suite_hid

        if suite.name != 'py3test':
            # Reason:  https://st.yandex-team.ru/DEVTOOLS-7032
            # TODO:    https://st.yandex-team.ru/DEVTOOLS-7033
            effective_suite_id = None
            effective_suite_hid = None

        for i, chunk in enumerate(suite.chunks):
            chunk_entry = copy.deepcopy(general_info)
            chunk_name = "{}".format(chunk.get_name())
            chunk_id = get_id(
                suite.project_path, suite.get_type(), test_type=suite.get_ci_type_name(), chunk_name=chunk_name
            )
            chunk_hid = get_hash_id(
                suite.project_path, suite.get_type(), test_type=suite.get_ci_type_name(), chunk_name=chunk_name
            )

            # TODO WIP
            chunk_entry.update(
                {
                    "chunk": True,
                    "id": chunk_id,
                    "hid": chunk_hid,
                    "links": get_test_logs(chunk.logs, suite.name, merger_out_dir),
                    "metrics": chunk.metrics,
                    "name": suite.get_type(),
                    "rich-snippet": build_container_comment(chunk),
                    "status": convert_test_status(chunk.get_status()),
                    "subtest_name": chunk_name,
                    "suite_hid": suite_hid,
                }
            )
            error_type = get_test_error_type(chunk.get_status())
            if error_type:
                chunk_entry["error_type"] = error_type
            if chunk.metrics.get("wall_time"):
                chunk_entry["duration"] = chunk.metrics.get("wall_time")

            for test in chunk.tests:
                # Don't make copy from suite_entry, it may contain or not some fields
                # and they may be inherited and not overwritten by mistake
                test_entry = copy.deepcopy(general_info)
                test_class_name = test.get_class_name() or "noname_suite"
                test_entry.update(
                    {
                        "id": get_id(
                            test.path or suite.project_path,
                            test.get_class_name(),
                            test.get_test_case_name(),
                            suite.get_ci_type_name(),
                            suite_id=effective_suite_id,
                        ),
                        # WIP: https://st.yandex-team.ru/DEVTOOLS-8716
                        "hid": get_hash_id(
                            test.path or suite.project_path,
                            test.get_class_name(),
                            test.get_test_case_name(),
                            suite.get_ci_type_name(),
                            suite_id=effective_suite_hid,
                        ),
                        "suite_hid": suite_hid,
                        "chunk_hid": chunk_hid,
                        # there may be Y_UNIT_TEST_SUITE() without arguments, but we must not leave 'name' empty
                        "name": test_class_name,
                        "subtest_name": truncate_test_name(test_class_name, test.get_test_case_name()),
                        "rich-snippet": test.comment,
                        "duration": test.elapsed,
                        "links": get_test_logs(test.logs, suite.name, merger_out_dir),
                        "status": convert_test_status(test.status),
                    }
                )
                if test.path:
                    test_entry["path"] = test.path

                if test.metrics:
                    test_entry["metrics"].update(test.metrics)

                if test.tags:
                    test_entry["tags"] = list(set(test_entry["tags"] + test.tags))

                # Protocol's "status" field is binary: good/fail
                # However suites/tests/subtests operates with status that may be good/fail/missing/crashed/skipped
                # This extra information is saved in the field "error_type"
                error_type = get_test_error_type(test.status)
                if error_type:
                    test_entry["error_type"] = error_type

                if test.is_diff_test and test.status == Status.GOOD:
                    test_result = {}
                    try:
                        for key, obj in six.iteritems(test.result):
                            obj = external.ExternalDataInfo(obj)
                            test_result[key] = {
                                "hash": obj.checksum,
                                "url": obj.path,
                                "size": obj.size,
                            }
                    except Exception:
                        logger.exception("Error while saving diff_test result")
                    test_entry["result"] = test_result

                # XXX: the order below matters! (see YA-2468 for more info)
                entries.append(test_entry)
            entries.append(chunk_entry)
        entries.append(suite_entry)
    return entries


def load_prototype_map_from_file(path):
    with open(path) as prototype_file:
        results = json.load(prototype_file)
        mapping = collections.defaultdict(list)

        for result in results:
            mapping[result['uid']].append(result)

        return mapping
