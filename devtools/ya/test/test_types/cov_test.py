# coding: utf-8

import os

from test.util import tools, shared
from test import const, common as test_common
from test.system import process
from test.test_types import common as common_types


class CoverageExtractorTestSuite(common_types.AbstractTestSuite):
    def __init__(self, *args, **kwargs):
        super(CoverageExtractorTestSuite, self).__init__(*args, **kwargs)
        self.add_python_before_cmd = False

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            retry,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        return tools.get_test_tool_cmd(
            opts, 'run_coverage_extractor', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            '--binary',
            self.binary_path('$(BUILD_ROOT)'),
            '--tracefile',
            os.path.join(test_work_dir, const.TRACE_FILE_NAME),
            '--output-dir',
            os.path.join(test_work_dir, const.TESTING_OUT_DIR_NAME),
            '--project-path',
            self.project_path,
            '--verbose',
        ]

    def get_list_cmd(self, arc_root, build_root, opts):
        return self.get_run_cmd(opts) + ['--list']

    def get_computed_test_names(self, opts):
        return ["{}::test".format(self.get_type_name())]

    @classmethod
    def list(cls, cmd, cwd):
        return [
            test_common.SubtestInfo(*info)
            for info in shared.get_testcases_info(process.execute(cmd, check_exit_code=False, cwd=cwd))
        ]

    @property
    def supports_canonization(self):
        return False

    @classmethod
    def get_type_name(cls):
        return 'coverage_extractor'

    @property
    def name(self):
        return self.get_type_name()

    @classmethod
    def support_retries(cls):
        return False

    @property
    def supports_coverage(self):
        return True
