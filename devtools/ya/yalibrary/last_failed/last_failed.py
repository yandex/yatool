import logging
import os
import time

import exts.windows
import exts.tmp
import exts.yjson as json

from yalibrary.store import new_store
from yalibrary.runner import uid_store
from devtools.ya.test.test_types.common import AbstractTestSuite

logger = logging.getLogger(__name__)
STATUS_STORE_SIZE = 10 * 1024 * 1024  # 10MB
STATUS_STORE_TTL = 1  # last run


type StoreItemInfo = uid_store.UidStoreItemInfo | new_store.NewStoreItemInfo
type TestsStatuses = dict[str, str]


class SizeFilter:
    def __init__(self, size_limit: int) -> None:
        self.size_limit = size_limit
        self.total_size = 0
        self._items = {}

    def __call__(self, item: StoreItemInfo) -> bool:
        if item.uid in self._items:
            return False
        size = item.size
        self.total_size += size
        self._items[item.uid] = item
        return self.total_size < self.size_limit


class AgeFilter:
    def __init__(self, age_limit: int) -> None:
        self.now = time.time()
        self.age_limit = age_limit
        self.total_size = 0

    def __call__(self, item: StoreItemInfo) -> bool:
        leave_in_store = item.timestamp > self.now - self.age_limit
        if leave_in_store:
            self.total_size += item.size
        return leave_in_store


class StatusStore:
    def __init__(self, store_path: str) -> None:
        if exts.windows.on_win():
            self.store = uid_store.UidStore(store_path)
        else:
            self.store = new_store.NewStore(store_path)

    def put(self, uid: str, content: TestsStatuses) -> None:
        with exts.tmp.temp_file() as temp_file:
            with open(temp_file, 'w', encoding='utf8') as afile:
                json.dump(content, afile)
            self.store.put(uid, os.path.split(temp_file)[0], [temp_file])

    def get(self, uid: str) -> TestsStatuses | None:
        with exts.tmp.temp_dir() as tmp_dir:
            if self.store.try_restore(uid, tmp_dir):
                with open(os.path.join(tmp_dir, os.listdir(tmp_dir)[0]), encoding='utf-8') as afile:
                    return json.load(afile)
            else:
                return None

    def compact(self, max_size: int, ttl: int) -> None:
        mem_age_filter = AgeFilter(ttl)
        self.store.strip(mem_age_filter)
        if mem_age_filter.total_size > 2 * max_size:
            self.store.strip(SizeFilter(max_size))
        self.flush()

    def flush(self) -> None:
        self.store.flush()


def get_tests_restart_cache_dir(garbage_dir: str) -> str:
    return os.path.join(garbage_dir, 'cache', 'trc')


# TODO (v-korovin): Use Status from test_const

RERUN_FULL_CHUNK_STATUSES = (
    'crashed',
    'fail',
    'internal',
)


def _get_tests_statuses(trace_path: str, suite_hash: str) -> TestsStatuses:
    res = {}
    with open(trace_path) as read_file:
        for test_info in read_file:
            test_info = json.loads(test_info)

            if test_info_value := test_info.get('value'):
                if test_info['name'] == 'chunk-event':
                    # For full status list look into build/plugins/lib/test_const/__init__.py::Status
                    test_statuses = [error[0] for error in test_info_value.get('errors', tuple())]

                    if not test_statuses:
                        # No errors in chunk-event means all ok
                        pass
                    elif failed_statuses := set(
                        status for status in test_statuses if status in RERUN_FULL_CHUNK_STATUSES
                    ):
                        # If a chunk fails we mark the whole suite as failed. We can't reliably rerun chunks
                        # because the set of tests in a chunk can change between runs moving test files
                        # that trigger the chunk failure to other chunks.
                        logger.debug(
                            "Mark FULL CHUNK %s as failed because of chunk-event error statuses %s",
                            suite_hash,
                            failed_statuses,
                        )
                        # TODO: Use Status from test_const and merge_statuses_info?
                        return {suite_hash: ','.join(failed_statuses)}
                    else:
                        # We want rerun tests in chunks granullary if `timeout`, `not_launched` and other chunk-event statuses happens
                        pass

                if all([key in test_info_value for key in ['status', 'subtest', 'class']]):
                    test_name = test_info_value['class'] + '::' + test_info_value['subtest']
                    res[test_name] = test_info_value['status']

    # TODO: if res is empty, why?
    return res


def _get_trace_path(res_list) -> str | None:
    for res_elem in res_list:
        if 'trace_file' in res_elem:
            return res_elem['trace_file']


TEST_GOOD_STATUSES = {
    'good',
    'xfail',
    'skipped',
    'xfaildiff',
    'xpass',
}


def _merge_statuses_info(old_info: TestsStatuses | None, new_info: TestsStatuses) -> TestsStatuses:
    if old_info is None:
        old_info = {}
    result_info = old_info.copy()
    for test_name, status in new_info.items():
        if test_name in old_info and status in TEST_GOOD_STATUSES:
            result_info.pop(test_name)
        if test_name not in old_info and status not in TEST_GOOD_STATUSES:
            result_info[test_name] = status
    return result_info


def _get_suite_statuses(res, suite: AbstractTestSuite, suite_hash: str) -> TestsStatuses:
    statuses_info = {}
    for uid in suite.result_uids:
        if uid not in res:
            continue
        trace_path = _get_trace_path(res[uid])
        if not trace_path:
            continue
        trace_content = _get_tests_statuses(trace_path, suite_hash)
        statuses_info.update(trace_content)
    return statuses_info


def cache_test_statuses(res, tests: list[AbstractTestSuite], garbage_dir: str, last_failed_tests: bool) -> None:
    status_storage = StatusStore(get_tests_restart_cache_dir(garbage_dir))
    status_storage.compact(STATUS_STORE_SIZE, STATUS_STORE_TTL)
    all_suite_res: list[tuple[str, TestsStatuses]] = []
    is_all_empty = True
    for suite in tests:
        suite_hash: str = suite.get_state_hash()
        new_statuses_info = _get_suite_statuses(res, suite, suite_hash)
        if new_statuses_info:
            logger.debug("%s status info: %s", suite, new_statuses_info)
        all_suite_res.append(
            (
                suite_hash,
                new_statuses_info,
            )
        )
        if new_statuses_info:
            is_all_empty = False

    if is_all_empty and last_failed_tests:
        logger.info(
            "you probably renamed or deleted all known failed tests.\n"
            "      The status store will be cleared.\n"
            "      Next test run will restart all tests"
        )
        for h, _ in all_suite_res:
            status_storage.put(h, {})
    else:
        for h, new_info in all_suite_res:
            old_statuses_info = status_storage.get(h)
            res_info = _merge_statuses_info(old_statuses_info, new_info)
            status_storage.put(h, res_info)
    status_storage.flush()
