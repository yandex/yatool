# coding: utf-8
import json
import logging
import os
import re

from . import trace_comment
from exts import func
from test import common
from test import const


logger = logging.getLogger(__name__)


def get_display():
    # noinspection PyUnresolvedReferences
    import app_ctx

    return app_ctx.display


class _FileDisplay(object):

    def __init__(self, path):
        self._path = path

    def emit_message(self, s):
        with open(self._path, "a") as f:
            f.write("{}\n".format(s))


class ConsoleReporter(object):

    def __init__(
            self,
            show_passed=True,
            show_test_cwd=False,
            show_metrics=False,
            truncate=True,
            show_deselected=False,
            omitted_test_statuses=None,
            show_suite_logs_for_tags=None,
            out_path=None,
            show_skipped=None,
            display=None,
    ):
        self._show_passed = show_passed
        self._show_test_cwd = show_test_cwd
        self._truncate = truncate
        self._show_metrics = show_metrics
        self._show_deselected = show_deselected
        self._show_skipped = show_skipped
        self._show_suite_logs_for_tags = show_suite_logs_for_tags or []
        self._show_suite_logs_for_tags = set(self._show_suite_logs_for_tags)
        omitted_test_statuses = omitted_test_statuses or []
        self._omitted_test_statuses = {const.Status.BY_NAME[x] for x in omitted_test_statuses}
        if display:
            self._display = display
        else:
            self._display = get_display() if out_path is None else _FileDisplay(out_path)

    @func.memoize()
    def get_status_marker(self, status):
        return "[[%s]]" % const.StatusColorMap[status]

    def on_tests_start(self):
        pass

    def on_tests_finish(self, test_suites):
        lines = []
        status_pattern = "[[{marker}]]\t{count} - {status}[[rst]]"
        if test_suites:
            count = len(test_suites)
            suite_results = [s.get_status() for s in test_suites]
            lines.append('\nTotal {} suite{}:'.format(count, '' if count == 1 else 's'))
            lines.append("\n".join(common.get_formatted_statuses(suite_results.count, status_pattern)))
        else:
            lines.append('\nTotal 0 suites')

        test_results = [test.status for suite in test_suites for test in suite.tests]
        test_count = len(test_results)
        if test_count:
            lines.append('Total {} test{}:'.format(test_count, '' if test_count == 1 else 's'))
            lines.append("\n".join(common.get_formatted_statuses(test_results.count, status_pattern)))
        else:
            lines.append('Total 0 tests')
        self._display.emit_message('\n'.join(lines))

    def get_formatted_metrics(self, entry):
        return json.dumps(entry.metrics, sort_keys=True, indent=4)

    def on_tests_interrupt(self):
        self._display.emit_message('[[bad]]Keyboard interrupt[[rst]]')

    def _get_logs_frame(self, status, entry):
        if status not in (
            const.Status.CRASHED,
            const.Status.FAIL,
            const.Status.FLAKY,
            const.Status.GOOD,
            const.Status.INTERNAL,
            const.Status.NOT_LAUNCHED,
            const.Status.TIMEOUT,
            const.Status.XFAIL,
            const.Status.XPASS,
        ) or not entry.logs:
            return ""
        lines = []
        for name in sorted(entry.logs):
            # Don't print logs for specific runs.
            # All data is stored on local host and easily accessible.
            if re.search(r"_run\d+$", name):
                continue
            log_path = entry.logs[name]
            if os.sep != "/":
                if log_path.startswith("http"):
                    log_path = log_path.replace(os.sep, "/")  # if it is a link - change all seps to /
                else:
                    log_path = log_path.replace("/", os.sep)  # some parts like project_path always come with linux slashes
            lines.append("[[imp]]{name}:{marker} [[path]]{filename}[[rst]]".format(
                marker=self.get_status_marker(const.Status.TO_STR[status]), name=name.capitalize(), filename=log_path))
        return "\n".join(lines)

    def on_test_suite_finish(self, test_suite):
        lines = []
        show_suite = False

        def dump_suite_header(test_suite):
            msg = '\n[[imp]]{}[[rst]] <[[unimp]]{}[[rst]]>'.format(test_suite.project_path, test_suite.get_type())
            if test_suite.test_size and test_suite.test_size != const.TestSize.Small:
                msg += ' [size:[[imp]]{}[[rst]]]'.format(test_suite.test_size)
            if len(test_suite.chunks) > 1:
                msg += ' nchunks:[[imp]]{}[[rst]]'.format(len(test_suite.chunks))
            if test_suite.tags:
                msg += ' [tags: [[imp]]{}[[rst]]]'.format(", ".join(sorted(test_suite.tags)))
            lines.append(msg)

        def dump_container_info(container):
            if container.metrics and self._show_metrics:
                lines.append(self.get_formatted_metrics(container))

            comment = container.get_comment()
            if self._truncate:
                comment = trace_comment.truncate_comment(comment, const.CONSOLE_SNIPPET_LIMIT)
            if comment:
                lines.append("{}[[rst]]".format(comment))

            if comment or set(test_suite.tags) & self._show_suite_logs_for_tags:
                # suite logs (don't print logs for suites without useful comment)
                logs_frame = self._get_logs_frame(suite_status, container)
                if logs_frame:
                    lines.append(logs_frame)

        def dump_chunk_header(chunk):
            ntests = len(chunk.tests)
            msg = '{} [[imp]]{}[[rst]] ran [[imp]]{}[[rst]] test{}'.format(
                "-" * 6,
                chunk.get_name(),
                ntests,
                '' if ntests == 1 else 's'
            )

            if chunk.metrics:
                timings = []
                for label, name in [
                    ('setup', 'suite_setup_environment_(seconds)'),
                    ('recipes', 'suite_prepare_recipes_(seconds)'),
                    ('test', 'suite_wrapper_execution_(seconds)'),
                    ('recipes', 'suite_stop_recipes_(seconds)'),
                    ('canon', 'suite_canonical_data_verification_(seconds)'),
                    ('postprocess', 'suite_postprocess_(seconds)'),
                ]:
                    if name in chunk.metrics:
                        val = chunk.metrics[name]
                        if val > 0.009:
                            timings.append('{}:{:0.2f}s'.format(label, val))
                if 'wall_time' in chunk.metrics:
                    if timings:
                        timings.insert(0, '-')
                    timings.insert(0, 'total:{:0.2f}s'.format(chunk.metrics['wall_time']))
                if timings:
                    msg += ' ({})'.format(' '.join(timings))

            lines.append(msg)

        def dump_test_case_info(test_case):
            status = const.Status.TO_STR[test_case.status]
            msg = '[{}{}[[rst]]]'.format(self.get_status_marker(status), status)
            msg += ' [[imp]]{}[[rst]]::{}'.format(test_case.get_class_name(), test_case.get_test_case_name())

            if test_case.tags:
                msg += ' [tags: [[imp]]{}[[rst]]]'.format(", ".join(sorted(test_case.tags)))

            if test_suite.target_platform_descriptor:
                msg += ' [[[alt1]]{}[[rst]]]'.format(test_suite.target_platform_descriptor)
            msg += ' ({:0.2f}s)'.format(test_case.elapsed)
            if test_case.metrics and self._show_metrics:
                msg += "\n" + self.get_formatted_metrics(test_case)
            if test_case.comment:
                comment = test_case.comment + "[[rst]]"
                if self._truncate:
                    comment = trace_comment.truncate_comment(comment, const.CONSOLE_SNIPPET_LIMIT)
                if "\n" in comment or self._show_metrics:
                    msg += "\n" + comment
                else:
                    msg += " " + comment
                msg += "[[rst]]"

            logs_frame = self._get_logs_frame(test_case.status, test_case)
            if logs_frame:
                msg += "\n" + logs_frame

            if self._show_test_cwd and test_case.cwd:
                msg += "\n[[imp]]Work dir:{} [[path]]{}[[rst]]".format(self.get_status_marker(status), test_case.cwd)
            lines.append(msg)

        # suite header
        suite_status = test_suite.get_status()
        if test_suite.get_comment() or suite_status not in self._omitted_test_statuses or self._show_passed:
            show_suite = True
            dump_suite_header(test_suite)
            dump_container_info(test_suite)

        if test_suite.chunks:
            significant_tests = [[] for _ in range(len(test_suite.chunks))]
            for chunk_idx, chunk in enumerate(test_suite.chunks):
                for test_case in chunk.tests:
                    if test_case.status in self._omitted_test_statuses and not self._show_passed:
                        continue
                    if test_case.status == const.Status.DESELECTED and not self._show_deselected:
                        continue
                    if test_case.status == const.Status.SKIPPED and not self._show_skipped:
                        continue
                    significant_tests[chunk_idx].append(test_case)

            for chunk_idx, tests in enumerate(significant_tests):
                chunk = test_suite.chunks[chunk_idx]
                if tests or chunk.has_comment():
                    dump_chunk_header(chunk)
                    dump_container_info(chunk)
                for test_case in tests:
                    dump_test_case_info(test_case)

        # suite footer
        if show_suite:
            status = const.Status.TO_STR[suite_status]
            length = 6
            msg = '{} {}{}[[rst]]'.format("-" * length, self.get_status_marker(status), status.upper())

            test_results = [test.status for test in test_suite.tests]
            if test_results:
                msg += ': {}[[rst]]'.format(", ".join(common.get_formatted_statuses(test_results.count, "[[{marker}]]{count} - {status}[[rst]]")))
            msg += ' [[imp]]{}[[rst]]'.format(test_suite.project_path)

            lines.append(msg)

        if lines:
            self._display.emit_message('\n'.join(lines))

    def on_test_case_started(self, test_name):
        pass

    def on_test_case_finished(self, test_case):
        pass

    def on_message(self, text):
        self._display.emit_message(text)

    def on_warning(self, text):
        self._display.emit_message("[[warn]]{}[[rst]]".format(text))
