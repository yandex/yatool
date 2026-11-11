import abc
import collections
import contextlib
import copy
import itertools
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
import yalibrary.display

import typing as tp

if tp.TYPE_CHECKING:
    import devtools.ya.build.build_result as build_result
    import devtools.ya.test.test_types.common as tt_common  # noqa


class ResultsListener(abc.ABC):
    """Handle node events, then EOF, and release resources independently of EOF.

    A common lifecycle lets disposable CI results and resident test models share
    YaMake finalization. The caller joins event producers before EOF or close;
    close must also work after partial setup, cancellation, or failed EOF.
    """

    @abc.abstractmethod
    def __call__(self, res: dict | None = None, build_stage: dict | None = None) -> None:
        pass

    def on_build_finished(self, result: 'build_result.BuildResult') -> None:
        pass

    def close(self) -> None:
        pass


class CallbackResultsListener(ResultsListener):
    """Explicitly adapt event-only callbacks to the listener lifecycle."""

    def __init__(self, callback: tp.Callable[..., None]) -> None:
        self._callback = callback

    def __call__(self, res: dict | None = None, build_stage: dict | None = None) -> None:
        self._callback(res=res, build_stage=build_stage)


class TestSummary(tp.NamedTuple):
    suite_count: int
    failed: bool
    internal_error: bool

    def exit_code(self, test_fail_exit_code: int = test_const.TestRunExitCode.Failed) -> int:
        if self.internal_error:
            return test_const.TestRunExitCode.InfrastructureError
        return int(test_fail_exit_code) if self.failed else 0


def summarize_suites(suites: tp.Iterable['tt_common.AbstractTestSuite']) -> TestSummary:
    suite_count = 0
    failed = False
    internal_error = False
    for suite in suites:
        suite_count += 1
        if not suite.is_skipped():
            status = suite.get_status()
            failed |= status != test_const.Status.GOOD
            internal_error |= status == test_const.Status.INTERNAL
    return TestSummary(suite_count, failed, internal_error)


class TestResultsListener(ResultsListener):
    @abc.abstractmethod
    def summary(self) -> TestSummary:
        """Return the current outcome without requiring resident suite models."""


class TestReportContext(tp.NamedTuple):
    output_root: str
    skipped_suites: tp.Sequence['tt_common.AbstractTestSuite'] = ()
    skipped_only: bool = False
    list_tests: bool = False


def extract_node_stderr(res, opts) -> tuple[str, list]:
    """Return the stderr of a failed node and the links to its logs (in-house only)."""
    import app_config

    if app_config.in_house:
        from devtools.ya.yalibrary.yandex.distbuild import distbs

        return distbs.extract_stderr(res, opts.mds_read_account, download_stderr=opts.download_failed_nodes_stderr)
    stderrs = copy.copy(res.get('stderrs', []))
    if opts.arc_root:
        stderrs = [i.replace(opts.arc_root, '$(SOURCE_ROOT)') for i in stderrs]
    build_root = res.get('build_root')
    if build_root:
        stderrs = [i.replace(build_root, '$(BUILD_ROOT)') for i in stderrs]
    return '\n'.join(stderrs), []


class NodeFailureListener(ResultsListener):
    """Publish a ``fail`` event of a structured display for every failed build or test node.

    A test node exits non-zero only when the test tool itself fails (e.g. it
    cannot start): failed tests are the test reporter's, and the reporter sees
    nothing of such a crash, so the node output is published here with the
    ``test`` stage. Merge nodes are left to the test reporter; a uid is published once.
    A node broken by a failed dependency is skipped: the root cause is already
    in the stream and the node itself adds nothing but the dependency uid. The
    dependents of a root cause are marked when it fails; a broken intermediate
    node never runs and reports nothing, so the marking is transitive.
    """

    def __init__(self, graph, display, opts):
        self._display = display
        self._opts = opts
        self._graph = graph['graph']
        # Built on the first failure only: a green build pays nothing.
        self._nodes = None
        self._reversed_deps = None
        self._seen = set()
        self._broken = set()
        self._lock = threading.Lock()

    def __call__(self, res=None, build_stage=None):
        if not res or res.get('status', 0) == 0:
            return
        uid = res['uid']
        with self._lock:
            if self._nodes is None:
                self._index_graph()
            node = self._nodes.get(uid)
            if node is None or node.get('node-type') not in (None, 'test') or uid in self._seen:
                return
            self._seen.add(uid)
            if uid in self._broken:
                return
            self._mark_dependents_broken(uid)
        stderr, links = extract_node_stderr(res, self._opts)
        details = {
            'kind': node.get('kv', {}).get('p'),
            # Structured events cross a stdlib JSON boundary. The local graph
            # can supply CompactList, but this event field is a string array.
            'outputs': list(node.get('outputs', [])),
            'platform': bp.BuildPlan.node_platform(node),
            'exit_code': res.get('status'),
        }
        if links:
            details['logs'] = links
        stage = 'test' if 'node-type' in node else 'build'
        suite_path = node.get('kv', {}).get('path')
        # A test node kv path is `<project path>/<suite name>`: the project path matches the test reporter's events
        path = os.path.dirname(suite_path) if stage == 'test' and suite_path else bp.BuildPlan.node_name(node)
        self._display.emit_event(
            {
                'type': 'fail',
                'stage': stage,
                'path': path,
                'text': yalibrary.display.strip_markup(stderr),
                'details': details,
            }
        )

    def _index_graph(self):
        self._nodes = {}
        self._reversed_deps = defaultdict(list)
        for node in self._graph:
            self._nodes[node['uid']] = node
            for dep in set(node['deps']):
                self._reversed_deps[dep].append(node['uid'])

    def _mark_dependents_broken(self, uid):
        # type: (str) -> None
        stack = list(self._reversed_deps.get(uid, ()))
        while stack:
            dependent = stack.pop()
            if dependent in self._broken:
                continue
            self._broken.add(dependent)
            stack.extend(self._reversed_deps.get(dependent, ()))


class BuildResultsListener(ResultsListener):
    _logger = logging.getLogger('BuildResultsListener')

    def __init__(self, graph, report_generator, build_root, opts):
        self._lock = threading.Lock()
        self._build_metrics = st.make_targets_metrics(graph['graph'], {}, {})
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
                stderr, error_links = extract_node_stderr(res, self._opts)
                self._on_failed(res['uid'], stderr, error_links, res.get('exit_code', -99))
        if build_stage:
            self._on_trace_stage(build_stage)

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


class TestNodeListener(TestResultsListener):
    _logger = logging.getLogger('TestNodeListener')

    def __init__(self, tests, output_root, report_generator, *, stream_results=True, report_context=None):
        self._lock = threading.Lock()
        self._output_root = output_root
        self._tests = {}  # type: dict[str, tt_common.AbstractTestSuite]
        self._seen = set()
        self._report_generator = report_generator
        self._suites = tests
        self._stream_results = stream_results
        self._report_context = report_context or TestReportContext(output_root)

        if stream_results:
            for tst in tests:
                self._tests[tst.uid] = tst

    def __call__(self, res=None, build_stage=None):
        if not self._stream_results:
            return
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

    def on_build_finished(self, result: 'build_result.BuildResult') -> None:
        if self._report_generator is None:
            return
        context = self._report_context
        # Do not extend Context.stripped_tests in place: later full reports and
        # console output reuse that selection independently of JSONL reporting.
        suites = context.skipped_suites
        if not context.skipped_only and not context.list_tests:
            resolver = test_reports.TextTransformer(
                [("$(BUILD_ROOT)", context.output_root), ("$(SOURCE_ROOT)", "arcadia")]
            )
            test_result.fill_suites_results_from_build_result(self._suites, result, context.output_root, resolver)
            suites = itertools.chain(suites, self._suites)
        self._report_generator.add_tests_results(
            suites, result.build_errors, result.node_build_errors_links, defaultdict(list)
        )

    def summary(self) -> TestSummary:
        # Full reports and console preparation may still update these models
        # after EOF. Unlike CI's compact summary this view must remain live.
        return summarize_suites(self._suites)

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

        # A completed graph node has resolved its dependencies. Publish trace facts
        # before the streaming reporter freezes the suite result.
        test_result.record_test_preparation(suite)

        if self._report_generator is not None:
            self._report_generator.add_tests_results([suite], None, {}, defaultdict(list))


class SlotListener(ResultsListener):
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
            self._output_jsonl = None

        if not self._output_file:
            return

        try:
            with open(self._output_file, 'w') as f:
                f.write(json.dumps({'slot_time': self._slot_time}))
        except Exception as e:
            self._logger.error('Fail to save slot time: %s', e)

    def on_build_finished(self, result: 'build_result.BuildResult') -> None:
        self.finish()

    def close(self) -> None:
        if self._output_jsonl:
            self._output_jsonl.close()
            self._output_jsonl = None

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


class CompositeResultsListener(ResultsListener):
    def __init__(self, listeners=None):
        self._listeners: list[ResultsListener] = list(listeners or ())
        self._duration = collections.defaultdict(float)
        self._finished = False
        self._closed = False

    def add(self, listener: ResultsListener) -> None:
        self._listeners.append(listener)

    def __call__(self, *args, **kwargs):
        if self._finished or self._closed:
            return
        for listener in self._listeners:
            ts = time.time()
            try:
                listener(*args, **kwargs)
            finally:
                self._duration[type(listener).__name__] += time.time() - ts

    def on_build_finished(self, result: 'build_result.BuildResult') -> None:
        if self._finished or self._closed:
            return
        self._finished = True
        for listener in self._listeners:
            listener.on_build_finished(result)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        # All listeners must release their resources even if one close fails.
        with contextlib.ExitStack() as cleanup:
            for listener in self._listeners:
                cleanup.callback(listener.close)

    @property
    def stat(self):
        return {name: {"duration_sec": x} for name, x in self._duration.items()}


class FailedNodeListener(ResultsListener):
    NAMESPACE = 'devtools.ya.build.reports.failed_node_info'
    EVENT = 'node-failed'

    def __init__(self, evlog):
        self._writer = evlog.get_writer(self.NAMESPACE)

    def __call__(self, res=None, build_stage=None):
        if res and res.get('status', 0) != 0:
            self._writer(self.EVENT, uid=res['uid'], exit_code=res['status'])
