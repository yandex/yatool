# coding: utf-8

import os

from test import const, common as test_common
from test.test_types.py_test import LintTestSuite
from test.util import tools
from yalibrary.graph.const import BUILD_ROOT


class DetektReportTestSuite(LintTestSuite):
    def __init__(self, *args, **kwargs):
        super(DetektReportTestSuite, self).__init__(*args, **kwargs)
        self._files = self.get_suite_files()

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        work_dir = test_common.get_test_suite_work_dir(
            BUILD_ROOT,
            self.project_path,
            self.name,
            retry,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        return tools.get_test_tool_cmd(
            opts, 'run_detekt_report_check', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            "--report-path",
            self._get_files()[0],
            "--trace-path",
            os.path.join(work_dir, const.TRACE_FILE_NAME),
            "--project-path",
            self.project_path,
            "--verbose",
        ]

    def get_type(self):
        return "detekt"

    @property
    def class_type(self):
        return const.SuiteClassType.STYLE

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default
        return True
