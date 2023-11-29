import os
import six

import test.const
import test.util.tools
from test import common as test_common
from test.system import process
from test.test_types import common as common_types


class GoTestSuite(common_types.AbstractTestSuite):
    def support_splitting(self, opts=None):
        """
        Does test suite support splitting
        """
        return True

    @property
    def supports_test_parameters(self):
        return True

    def support_list_node(self):
        """
        Does test suite support list_node before test run
        """
        return True

    @property
    def supports_allure(self):
        return True

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)', self.project_path, self.name, retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        cmd = test.util.tools.get_test_tool_cmd(opts, 'run_go_test', self.global_resources, wrapper=True, run_on_target_platform=True) + [
            '--binary', self.binary_path('$(BUILD_ROOT)'),
            '--test-work-dir', test_work_dir,
            '--tracefile', os.path.join(test_work_dir, test.const.TRACE_FILE_NAME),
            '--modulo', str(self._modulo),
            '--modulo-index', str(self._modulo_index),
            '--partition-mode', self.get_fork_partition_mode(),
            '--output-dir', os.path.join(test_work_dir, test.const.TESTING_OUT_DIR_NAME),
            '--project-path', self.project_path,
            '--timeout', str(self.timeout),
            '--verbose',
        ]

        if opts and hasattr(opts, "tests_filters") and opts.tests_filters:
            for flt in opts.tests_filters:
                cmd += ['--test-filter', flt]

        for flt in self._additional_filters:
            cmd += ['--test-filter', flt]

        if opts and hasattr(opts, "test_params") and opts.test_params:
            for key, value in six.iteritems(opts.test_params):
                cmd += ['--test-param={}={}'.format(key, value)]

        for additional_arg in opts.test_binary_args:
            cmd += ["--test-binary-args={}".format(additional_arg)]

        if opts.gdb:
            cmd += ["--gdb-debug", "--gdb-path", os.path.join("$(GDB)", "gdb", "bin", "gdb")]

        if opts.dlv:
            cmd += ["--dlv-debug", "--dlv-path", os.path.join("$(DLV)", "dlv")]
            if opts.dlv_args:
                cmd += ["--dlv-args", opts.dlv_args]

        if self.wine_path:
            cmd += ["--wine-path", self.wine_path]

        if test.const.YaTestTags.GoNoSubtestReport in self.tags:
            cmd.append('--no-subtest-report')

        if test.const.YaTestTags.GoTotalReport in self.tags:
            cmd.append('--total-report')

        if opts.canonize_tests:
            cmd.append('--report-deselected')

        return cmd

    @classmethod
    def get_type_name(cls):
        return "go_test"

    def get_list_cmd(self, arc_root, build_root, opts):
        return self.get_run_cmd(opts) + ['--test-list']

    @classmethod
    def list(cls, cmd, cwd):
        return cls._get_subtests_info(process.execute(cmd, check_exit_code=False, cwd=cwd))

    @classmethod
    def _get_subtests_info(cls, list_cmd_result):
        result = []
        if list_cmd_result.exit_code == 0:
            for x in list_cmd_result.std_err.split():
                if test.const.TEST_SUBTEST_SEPARATOR in x:
                    testname, subtest = x.split(test.const.TEST_SUBTEST_SEPARATOR, 1)
                    result.append(test_common.SubtestInfo(testname, subtest))
            return result
        raise Exception(list_cmd_result.std_err)

    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]

    @property
    def supports_coverage(self):
        return True


class GoBenchSuite(GoTestSuite):
    def support_splitting(self, opts=None):
        """
        Does test suite support splitting
        """
        return False

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        go_bench_timeout = self.dart_info.get('GO_BENCH_TIMEOUT')
        cmd = super(GoBenchSuite, self).get_run_cmd(opts, retry, for_dist_build) + ["--bench-run"]
        if go_bench_timeout:
            cmd += ["--benchmark-timeout", go_bench_timeout]
        return cmd

    @classmethod
    def get_type_name(cls):
        return "go_bench"
