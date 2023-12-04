import os
import logging
import time

import exts.windows
import exts.tmp
import exts.yjson as json

from yalibrary.store import new_store
from yalibrary.runner import uid_store

from six import iteritems, ensure_text
import io


logger = logging.getLogger(__name__)
STATUS_STORE_SIZE = 10 * 1024 * 1024  # 10MB
STATUS_STORE_TTL = 1  # last run


class SizeFilter(object):
    def __init__(self, size_limit):
        self.size_limit = size_limit
        self.total_size = 0
        self._items = {}

    def __call__(self, item):
        if item.uid in self._items:
            return False
        size = item.size
        self.total_size += size
        self._items[item.uid] = item
        return self.total_size < self.size_limit


class AgeFilter(object):
    def __init__(self, age_limit):
        self.now = time.time()
        self.age_limit = age_limit
        self.total_size = 0

    def __call__(self, item):
        leave_in_store = item.timestamp > self.now - self.age_limit
        if leave_in_store:
            self.total_size += item.size
        return leave_in_store


class StatusStore:
    def __init__(self, store_path):
        if exts.windows.on_win():
            self.store = uid_store.UidStore(store_path)
        else:
            self.store = new_store.NewStore(store_path)

    def put(self, uid, content):
        with exts.tmp.temp_file() as temp_file:
            with io.open(temp_file, 'wt', encoding='utf8') as afile:
                # XXX: https://bugs.python.org/issue13769
                data = json.dumps(content, ensure_ascii=False)
                afile.write(ensure_text(data))
            self.store.put(uid, os.path.split(temp_file)[0], [temp_file])

    def get(self, uid):
        with exts.tmp.temp_dir() as tmp_dir:
            if self.store.try_restore(uid, tmp_dir):
                with io.open(os.path.join(tmp_dir, os.listdir(tmp_dir)[0]), 'rt', encoding='utf-8') as afile:
                    return json.load(afile)
            else:
                return None

    def compact(self, max_size, ttl):
        mem_age_filter = AgeFilter(ttl)
        self.store.strip(mem_age_filter)
        if mem_age_filter.total_size > 2 * max_size:
            self.store.strip(SizeFilter(max_size))
        self.flush()

    def flush(self):
        self.store.flush()


def get_tests_restart_cache_dir(garbage_dir):
    return os.path.join(garbage_dir, 'cache', 'trc')


def _get_trace_content(trace_path):
    res = {}
    with open(trace_path, 'r') as read_file:
        for test_info in read_file:
            test_info = json.loads(test_info)
            if 'value' in test_info and all([key in test_info['value'] for key in ['status', 'subtest', 'class']]):
                test_name = test_info['value']['class'] + '::' + test_info['value']['subtest']
                res[test_name] = test_info['value']['status']
    return res


def _get_trace_path(res_list):
    for res_elem in res_list:
        if 'trace_file' in res_elem:
            return res_elem['trace_file']


def _merge_statuses_info(old_info, new_info):
    if old_info is None:
        old_info = {}
    result_info = old_info.copy()
    for test_name, status in iteritems(new_info):
        if test_name in old_info and status in ['good', 'xfail', 'skipped']:
            result_info.pop(test_name)
        if test_name not in old_info and status not in ['good', 'xfail', 'skipped']:
            result_info[test_name] = status
    return result_info


def _get_suite_statuses(res, suite):
    statuses_info = {}
    for uid in suite.result_uids:
        if uid not in res:
            continue
        trace_path = _get_trace_path(res[uid])
        if not trace_path:
            continue
        trace_content = _get_trace_content(trace_path)
        statuses_info.update(trace_content)
    return statuses_info


def cache_test_statuses(res, tests, garbage_dir, last_failed_tests):
    status_storage = StatusStore(get_tests_restart_cache_dir(garbage_dir))
    status_storage.compact(STATUS_STORE_SIZE, STATUS_STORE_TTL)
    all_suite_res = []
    is_all_empty = True
    for suite in tests:
        params_hash = suite.get_state_hash()
        new_statuses_info = _get_suite_statuses(res, suite)
        if new_statuses_info:
            logger.debug("{} status info: {}".format(suite, new_statuses_info))
        all_suite_res.append(
            (
                params_hash,
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
