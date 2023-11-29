import sys

from . import dry


class StdErrReporter(dry.DryReporter):
    def __init__(self):
        super(StdErrReporter, self).__init__()
        self._started = set()
        self._finished = set()

    def on_test_case_started(self, test_name):
        if test_name not in self._started:
            sys.stderr.write("##[[alt1]]Test {} started[[rst]]\n".format(test_name))
            sys.stderr.flush()
            self._started.add(test_name)

    def on_test_case_finished(self, test_name):
        if test_name not in self._finished:
            sys.stderr.write("##[[alt1]]Test {} finished[[rst]]\n".format(test_name))
            sys.stderr.flush()
            self._finished.add(test_name)
