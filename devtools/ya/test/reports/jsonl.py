# coding: utf-8
"""Test report as ``fail`` events of a structured display.

The counterpart of ConsoleReporter for --output-style=jsonl: the same
selection policy (see selection.py), but every shown suite, chunk or test
case becomes one event instead of a block of text, and the status counters
go into the display for its ``finished`` event.

A failed test case or chunk of a regular test suite also gets ``rerun_filter``:
the ``-F`` argument to append to the original command to rerun only it. The ``finished`` event gets
next-step tips derived from the results (see _add_tips).

With --show-slowest-tests N the report also gets one ``slowest_tests`` event: the N
longest test cases of the run (see selection.slowest_test_cases), slowest first.
"""

import collections
import os
import re

import six

from devtools.ya.test import const
from devtools.ya.test.reports import selection
from devtools.ya.test.reports import trace_comment
import yalibrary.display

# Test case statuses which are not failures and need no rerun
_NOT_FAILED_STATUSES = frozenset([const.Status.GOOD, const.Status.XFAIL, const.Status.SKIPPED, const.Status.DESELECTED])
# -F selects a chunk only by a name of this form (see test/opts), the sole chunk cannot be selected
_CHUNK_FILTER_NAME = re.compile(r"^\[.*?] chunk$")
# Style checks whose findings `ya style` fixes with its default stylers
_STYLE_FIXABLE_LINTERS = frozenset(
    [
        const.PythonLinterName.Black,
        const.PythonLinterName.Ruff,
        'gofmt',
        const.CppLinterName.ClangFormat,
        const.CppLinterName.ClangFormatYT,
        const.CppLinterName.ClangFormat15,
        const.CppLinterName.ClangFormat18Vanilla,
        const.CppLinterName.ClangFormat18UserSessions,
    ]
)
# Suppress the -X tip for a single failure: its rerun filter is already the obvious next step
_MIN_FAILED_TESTS_FOR_RERUN_TIP = 2


class JsonlReporter(object):
    def __init__(
        self,
        display,
        omitted_test_statuses=None,
        show_deselected=False,
        show_skipped=False,
        truncate=True,
        arc_root=None,
        fail_fast=False,
        last_failed_tests=False,
        show_slowest=0,
    ):
        # type: (tp.Any, list[str] | None, bool, bool, bool, str | None, bool, bool, int) -> None
        self._display = display
        self._show_slowest = show_slowest
        # Makes the paths in tips independent of the cwd
        self._arc_root = arc_root
        self._omitted_test_statuses = {const.Status.BY_NAME[x] for x in omitted_test_statuses or []}
        self._show_deselected = show_deselected
        self._show_skipped = show_skipped
        self._truncate = truncate
        self._fail_fast = fail_fast
        self._last_failed_tests = last_failed_tests

    def on_tests_start(self):
        pass

    def on_test_suite_finish(self, test_suite):
        stage = 'style' if _is_style(test_suite) else 'test'
        suite_status = test_suite.get_status()
        if test_suite.has_comment() and suite_status not in self._omitted_test_statuses:
            details = {'level': 'suite', 'status': const.Status.TO_STR[suite_status]}
            self._emit_fail(stage, test_suite, test_suite.get_comment(), test_suite.logs, details)
        for chunk in test_suite.chunks:
            if chunk.has_comment():
                chunk_status = chunk.get_status()
                chunk_name = chunk.get_name()
                details = {
                    'level': 'chunk',
                    'status': const.Status.TO_STR[chunk_status],
                    'name': chunk_name,
                }
                filter_name = chunk_name if _CHUNK_FILTER_NAME.match(chunk_name) else None
                self._emit_fail(
                    stage,
                    test_suite,
                    chunk.get_comment(),
                    chunk.logs,
                    details,
                    self._rerun_filter(stage, chunk_status, filter_name),
                )
            selected = selection.select_test_cases(
                chunk, self._omitted_test_statuses, False, self._show_deselected, self._show_skipped
            )
            for test_case in selected:
                details = {
                    'level': 'test',
                    'status': const.Status.TO_STR[test_case.status],
                    'name': test_case.name,
                    'duration': test_case.elapsed,
                }
                self._emit_fail(
                    stage,
                    test_suite,
                    test_case.comment,
                    test_case.logs,
                    details,
                    self._rerun_filter(stage, test_case.status, test_case.name),
                )

    def on_tests_finish(self, test_suites):
        counts = collections.Counter()
        for suite in test_suites:
            for test_case in suite.tests:
                counts[const.Status.TO_STR[test_case.status]] += 1
        self._display.record_test_counts(dict(counts))
        slowest = selection.slowest_test_cases(test_suites, self._show_slowest)
        if slowest:
            self._display.emit_event(
                {'type': 'slowest_tests', 'tests': [self._slowest_entry(*pair) for pair in slowest]}
            )
        self._add_tips(test_suites)

    def on_tests_interrupt(self):
        self._display.emit_message('Keyboard interrupt', severity='error')

    def on_test_case_started(self, test_name):
        pass

    def on_test_case_finished(self, test_case):
        pass

    def on_message(self, text):
        self._display.emit_message(text)

    def on_warning(self, text):
        self._display.emit_message(text, severity='warning')

    def _add_tips(self, test_suites):
        # type: (list[tp.Any]) -> None
        """Suggest options that shorten the agent's next iteration over these results."""
        failed_suites = [suite for suite in test_suites if suite.get_status() not in _NOT_FAILED_STATUSES]
        style_paths = sorted(
            {
                self._absolute_path(suite.project_path)
                for suite in failed_suites
                if _is_style(suite) and suite.get_type() in _STYLE_FIXABLE_LINTERS
            }
        )
        if style_paths:
            self._display.add_tip(
                'ya_style',
                'formatting errors may be fixed automatically: run `ya style` on these paths',
                paths=style_paths,
            )

        # The reporter only runs when tests were requested, so the tip goes with every such run
        if not self._fail_fast:
            self._display.add_tip(
                'fail_fast',
                'add --fail-fast to stop at the first failed test suite and start fixing it sooner',
                flag='--fail-fast',
            )

        failed_tests = sum(
            1
            for suite in failed_suites
            if not _is_style(suite)
            for test_case in suite.tests
            if test_case.status not in _NOT_FAILED_STATUSES
        )
        if not self._last_failed_tests and failed_tests >= _MIN_FAILED_TESTS_FOR_RERUN_TIP:
            self._display.add_tip(
                'last_failed_tests',
                'add -X to rerun only the tests that failed; finish with a full run to catch regressions',
                flag='-X',
            )

    @staticmethod
    def _slowest_entry(test_suite, test_case):
        # type: (tp.Any, tp.Any) -> dict
        entry = {
            'path': test_suite.project_path,
            'name': test_case.name,
            'status': const.Status.TO_STR[test_case.status],
            'duration': test_case.elapsed,
        }
        if test_suite.multi_target_platform_run:
            entry['platform'] = test_suite.target_platform_descriptor
        return entry

    def _absolute_path(self, project_path):
        # type: (str) -> str
        """Make a project path independent of the cwd when the Arcadia root is known."""
        if self._arc_root:
            return os.path.join(self._arc_root, project_path)
        return project_path

    def _rerun_filter(self, stage, status, filter_name):
        # type: (str, int, str | None) -> str | None
        """The -F argument selecting only the failed test case or chunk, None if it cannot be targeted."""
        if stage != 'test' or not filter_name or status in _NOT_FAILED_STATUSES:
            return None
        return '-F {}'.format(six.moves.shlex_quote(filter_name))

    def _emit_fail(self, stage, test_suite, comment, logs, details, rerun_filter=None):
        # type: (str, tp.Any, str, dict, dict, str | None) -> None
        if self._truncate:
            comment = trace_comment.truncate_comment(comment, const.CONSOLE_SNIPPET_LIMIT)
        logs = selection.significant_logs(logs)
        if logs:
            details['logs'] = logs
        if test_suite.multi_target_platform_run:
            details['platform'] = test_suite.target_platform_descriptor
        event = {
            'type': 'fail',
            'stage': stage,
            'path': test_suite.project_path,
            'text': yalibrary.display.strip_markup(comment).strip(),
            'details': details,
        }
        if rerun_filter:
            event['rerun_filter'] = rerun_filter
        self._display.emit_event(event)


def _is_style(test_suite):
    # type: (tp.Any) -> bool
    return test_suite.get_ci_type_name() == 'style'
