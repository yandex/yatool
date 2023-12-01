class DryReporter(object):
    def on_tests_start(self):
        pass

    def on_tests_finish(self, test_suites):
        pass

    def on_tests_interrupt(self):
        pass

    def on_test_suite_finish(self, suite_name):
        pass

    def on_test_case_started(self, test_name):
        pass

    def on_test_case_finished(self, test_name):
        pass

    def on_message(self, text):
        pass
