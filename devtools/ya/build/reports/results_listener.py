import collections
import copy
import json
import logging
import os
import threading
import time
import traceback

import exts.fs

from collections import defaultdict

import devtools.ya.build.build_plan as bp
import devtools.ya.build.stat.graph_metrics as st
import devtools.ya.test.const as test_const
import devtools.ya.test.common as test_common
import devtools.ya.test.result as test_result
import devtools.ya.test.reports as test_reports

import typing as tp

if tp.TYPE_CHECKING:
    import devtools.ya.test.test_types.common as tt_common  # noqa


class BuildResultsListener:
    _logger = logging.getLogger('BuildResultsListener')

    def __init__(self, graph, report_generator, build_root, opts):
        self._lock = threading.Lock()
        self._build_metrics = st.make_targets_metrics(graph['graph'], {})
        self._report_generator = report_generator
        self._build_root = build_root
        self._notified = set()
        self._processed = set()
        self._reversed_deps = defaultdict(list)
        self._nodes = {}
        self._opts = opts

        for node in graph['graph']:
            self._nodes[node['uid']] = node
            for dep in set(node['deps']):
                self._reversed_deps[dep].append(node['uid'])

    def __call__(self, res=None, build_stage=None):
        if res is None:
            res = {}
        if 'status' in res or res.get('files'):
            if res.get('status', 0) == 0:
                self._on_completed(res['uid'], res)
            else:
                stderr, error_links = self._extract_stderr(res)
                self._on_failed(res['uid'], stderr, error_links, res.get('exit_code', -99))
        if build_stage:
            self._on_trace_stage(build_stage)

    def _extract_stderr(self, res):
        import app_config

        if app_config.in_house:
            from devtools.ya.yalibrary.yandex.distbuild import distbs

            return distbs.extract_stderr(
                res, self._opts.mds_read_account, download_stderr=self._opts.download_failed_nodes_stderr
            )
        else:
            stderrs = copy.copy(res.get('stderrs', []))
            if self._opts.arc_root:
                stderrs = [i.replace(self._opts.arc_root, '$(SOURCE_ROOT)') for i in stderrs]
            build_root = res.get('build_root')
            if build_root:
                stderrs = [i.replace(build_root, '$(BUILD_ROOT)') for i in stderrs]
            return '\n'.join(stderrs), []

    def _on_completed(self, uid, res):
        if 'node-type' not in self._nodes[uid]:
            self._on_build_node_completed(uid)
        else:
            self._logger.debug('Unknown node %s is completed', uid)

    def _on_failed(self, uid, error, error_links, exit_code):
        if 'node-type' not in self._nodes[uid]:
            self._on_build_node_failed(uid, error, error_links, exit_code)
        elif self._nodes[uid]['node-type'] == 'merger':
            self._on_merge_node_failed(uid, error)
        elif self._nodes[uid]['node-type'] == 'test':
            self._on_test_node_failed(uid, error)
        else:
            self._logger.debug('Unknown node %s is failed', uid)

    def _on_trace_stage(self, build_stage):
        self._report_generator.add_stage(build_stage)

    def _is_module(self, uid):
        return 'module_type' in self._nodes[uid].get('target_properties', {})

    def _resolve_target(self, uid):
        node = self._nodes[uid]
        return bp.BuildPlan.node_name(node), bp.BuildPlan.node_platform(node), bp.BuildPlan.get_module_tag(node)

    def _on_test_node_failed(self, uid, _):
        self._logger.debug('Test node %s is failed', uid)

    def _on_merge_node_failed(self, uid, _):
        self._logger.debug('Merge node %s is failed', uid)

    def _on_build_node_failed(self, uid, error, error_links, exit_code):
        def notify(u, e, a):
            if u not in self._notified:
                target_name, target_platform, module_tag = self._resolve_target(u)
                self._report_generator.add_build_result(
                    u, target_name, target_platform, [e], self._build_metrics.get(u, {}), module_tag, a, exit_code
                )
                self._notified.add(u)

        def make_broken_by_message(u):
            target_name, _, _ = self._resolve_target(u)
            return 'Depends on broken targets:\n{}'.format(target_name)

        def mark_failed(u, broken_dep):
            if u in self._processed:
                return

            self._processed.add(u)
            self._logger.debug('Node %s was broken by %s', u, uid)

            if self._is_module(u):
                msg = make_broken_by_message(broken_dep) if broken_dep else error
                links = [] if broken_dep else [error_links]
                notify(u, msg, links)
                broken_dep = broken_dep or u

            for reversed_dep in sorted(self._reversed_deps.get(u, tuple())):
                mark_failed(reversed_dep, broken_dep)

        with self._lock:
            mark_failed(uid, None)

    def _on_build_node_completed(self, uid):
        with self._lock:
            if self._is_module(uid) and uid not in self._notified:
                target_name, target_platform, module_tag = self._resolve_target(uid)
                self._report_generator.add_build_result(
                    uid, target_name, target_platform, [], self._build_metrics.get(uid, {}), module_tag, [], 0
                )
                self._notified.add(uid)


class TestNodeListener:
    _logger = logging.getLogger('TestNodeListener')

    def __init__(self, tests, output_root, report_generator):
        self._lock = threading.Lock()
        self._output_root = output_root
        self._tests = {}  # type: dict[str, tt_common.AbstractTestSuite]
        self._seen = set()
        self._report_generator = report_generator

        for tst in tests:
            self._tests[tst.uid] = tst

    def __call__(self, res=None, build_stage=None):
        if res is None:
            res = {}
        if 'status' in res or res.get('files'):
            uid = res['uid']
            if uid in self._tests:
                status = res.get('status', 0)
                if status == 0:
                    self._on_test_node_completed(uid, res)
                else:
                    self._on_test_node_failed(uid, status)

    def set_report_generator(self, report_generator):
        self._report_generator = report_generator

    def _on_test_node_failed(self, uid, status):
        self._logger.debug('Test node %s is failed. Status: %s', uid, status)

    def _on_test_node_completed(self, uid, res):
        with self._lock:
            if 'build_root' not in res or uid in self._seen:
                return
            self._seen.add(uid)

        suite = self._tests[uid]
        build_root = res['build_root']
        work_dir = test_common.get_test_suite_work_dir(
            build_root,
            suite.project_path,
            suite.name,
            target_platform_descriptor=suite.target_platform_descriptor,
            multi_target_platform_run=suite.multi_target_platform_run,
        )
        suite.set_work_dir(work_dir)
        if 'links_map' in res:
            suite.update_links_map(res['links_map'])

        resolver = test_reports.TextTransformer(
            [("$(BUILD_ROOT)", self._output_root or build_root), ("$(SOURCE_ROOT)/", "")]
        )
        result = test_result.TestPackedResultView(work_dir)
        try:
            suite.load_run_results(result.trace_report_path, resolver)
        except Exception:
            msg = "Infrastructure error - contact devtools@.\nFailed to load suite results:{}\n".format(
                traceback.format_exc()
            )
            suite.add_suite_error(msg, test_const.Status.INTERNAL)
            logging.debug(msg)

        if self._report_generator is not None:
            self._report_generator.add_tests_results([suite], None, {}, defaultdict(list))


class SlotListener:
    _logger = logging.getLogger('SlotListener')
    SLOT_TIME_FILE = 'slot_time.json'  # XXX: need for autocheck branch compatibility, remove after 13.04.2025
    SLOT_TIME_JSONL = 'slot_data.jsonl'

    def __init__(self, statistics_out_dir):
        self._output_file = None
        self._output_jsonl = None
        if statistics_out_dir:
            if not os.path.exists(statistics_out_dir):
                exts.fs.create_dirs(statistics_out_dir)
            self._output_file = os.path.join(statistics_out_dir, SlotListener.SLOT_TIME_FILE)
            self._output_jsonl = open(os.path.join(statistics_out_dir, SlotListener.SLOT_TIME_JSONL), 'w')

        self._slot_time = 0  # milliseconds

    def finish(self):
        if self._output_jsonl:
            self._output_jsonl.write(json.dumps({'total_slot_time': self._slot_time}) + '\n')
            self._output_jsonl.close()

        if not self._output_file:
            return

        try:
            with open(self._output_file, 'w') as f:
                f.write(json.dumps({'slot_time': self._slot_time}))
        except Exception as e:
            self._logger.error('Fail to save slot time: %s', e)

    def __call__(self, res=None, build_stage=None):
        if not res:
            return

        uid_slot_time = 0
        for process_result in res.get('process_results', []):
            self._slot_time += process_result.get('slot_time', 0)
            uid_slot_time += process_result.get('slot_time', 0)

        try:
            if self._output_jsonl:
                self._output_jsonl.write(
                    json.dumps({'uid': res['uid'], 'slot_time': uid_slot_time}, sort_keys=True) + '\n'
                )
        except Exception:
            self._logger.exception('Fail to save slot time by uid from %s', res)


class CompositeResultsListener:
    def __init__(self, listeners=None):
        self._listeners = []
        self._listeners.extend(listeners)
        self._duration = collections.defaultdict(float)

    def add(self, listener):
        self._listeners.append(listener)

    def __call__(self, *args, **kwargs):
        for listener in self._listeners:
            ts = time.time()
            try:
                listener(*args, **kwargs)
            finally:
                self._duration[type(listener).__name__] += time.time() - ts

    @property
    def stat(self):
        return {name: {"duration_sec": x} for name, x in self._duration.items()}


class FailedNodeListener:
    NAMESPACE = 'devtools.ya.build.reports.failed_node_info'
    EVENT = 'node-failed'

    def __init__(self, evlog):
        self._writer = evlog.get_writer(self.NAMESPACE)

    def __call__(self, res=None, build_stage=None):
        if res and res.get('status', 0) != 0 and 'exit_code' in res:
            self._writer(self.EVENT, uid=res['uid'], exit_code=res['exit_code'])
