# coding: utf-8
"""Test report as ``fail`` events of a structured display.

The counterpart of ConsoleReporter for --output-style=jsonl: the same
selection policy (see selection.py), but every shown suite, chunk or test
case becomes one event instead of a block of text, and the status counters
go into the display for its ``finished`` event.

A failed test case or chunk of a regular test suite also gets ``rerun``:
a ``ya test`` command which reruns only it.
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


class JsonlReporter(object):
    def __init__(
        self,
        display,
        omitted_test_statuses=None,
        show_deselected=False,
        show_skipped=False,
        truncate=True,
        arc_root=None,
    ):
        # type: (tp.Any, list[str] | None, bool, bool, bool, str | None) -> None
        self._display = display
        # Makes the rerun command independent of the cwd
        self._arc_root = arc_root
        self._omitted_test_statuses = {const.Status.BY_NAME[x] for x in omitted_test_statuses or []}
        self._show_deselected = show_deselected
        self._show_skipped = show_skipped
        self._truncate = truncate

    def on_tests_start(self):
        pass

    def on_test_suite_finish(self, test_suite):
        stage = 'style' if test_suite.get_ci_type_name() == 'style' else 'test'
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
                rerun_filter = chunk_name if _CHUNK_FILTER_NAME.match(chunk_name) else None
                self._emit_fail(
                    stage,
                    test_suite,
                    chunk.get_comment(),
                    chunk.logs,
                    details,
                    self._rerun(stage, test_suite, chunk_status, rerun_filter),
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
                    self._rerun(stage, test_suite, test_case.status, test_case.name),
                )

    def on_tests_finish(self, test_suites):
        counts = collections.Counter()
        for suite in test_suites:
            for test_case in suite.tests:
                counts[const.Status.TO_STR[test_case.status]] += 1
        self._display.record_test_counts(dict(counts))

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

    def _rerun(self, stage, test_suite, status, rerun_filter):
        # type: (str, tp.Any, int, str | None) -> str | None
        """The command rerunning only the failed test case or chunk, None if it cannot be targeted."""
        if stage != 'test' or not rerun_filter or status in _NOT_FAILED_STATUSES:
            return None
        target = test_suite.project_path
        if self._arc_root:
            target = os.path.join(self._arc_root, target)
        quote = six.moves.shlex_quote
        return 'ya test {} -F {}'.format(quote(target), quote(rerun_filter))

    def _emit_fail(self, stage, test_suite, comment, logs, details, rerun=None):
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
        if rerun:
            event['rerun'] = rerun
        self._display.emit_event(event)
