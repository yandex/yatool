import sys

from . import dry
from devtools.ya.test import const


class StdErrReporter(dry.DryReporter):
    def __init__(self, chunk_info=None):
        super(StdErrReporter, self).__init__()
        self._started = set()
        self._finished = set()
        self._suites_finished = set()
        self._chunk_info = chunk_info

    @staticmethod
    def _get_suite_name(test_suite):
        # TODO(@v-korovin): tracefile.finalize() calls on_test_suite_finish with a facility.Suite
        # which lacks project_path/name attributes (unlike the real test suite objects).
        # Consider adding these attributes to facility.Suite or passing the real suite to finalize().
        return getattr(test_suite, 'project_path', None) or getattr(test_suite, 'name', None)

    def _get_suite_display_name(self, test_suite):
        suite_name = self._get_suite_name(test_suite)
        if not suite_name:
            return None
        if self._chunk_info:
            return "{} [{} chunk]".format(suite_name, self._chunk_info)
        return suite_name

    @staticmethod
    def _format_status_message(prefix, name, status):
        status_str = const.Status.TO_STR.get(status, 'unknown')
        try:
            color_marker = const.StatusColorMap[status_str]
        except KeyError:
            color_marker = 'alt1'
        return "##[[{}]]{} {} {}[[rst]]\n".format(color_marker, prefix, name, status_str)

    def on_test_suite_start(self, test_suite):
        display_name = self._get_suite_display_name(test_suite)
        if display_name:
            sys.stderr.write("##[[alt1]]Suite {} started[[rst]]\n".format(display_name))
            sys.stderr.flush()

    def on_test_case_started(self, test_name):
        if test_name not in self._started:
            sys.stderr.write("##[[alt1]]Test {} started[[rst]]\n".format(test_name))
            sys.stderr.flush()
            self._started.add(test_name)

    def on_test_case_finished(self, test_case):
        test_name = test_case.name

        if test_name not in self._finished:
            sys.stderr.write(self._format_status_message("Test", test_name, test_case.status))
            sys.stderr.flush()
            self._finished.add(test_name)

    def on_test_suite_finish(self, test_suite):
        display_name = self._get_suite_display_name(test_suite)
        if not display_name:
            return

        if display_name not in self._suites_finished:
            sys.stderr.write(self._format_status_message("Suite", display_name, test_suite.get_status()))
            sys.stderr.flush()
            self._suites_finished.add(display_name)
