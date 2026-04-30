import os
import six
import json

import devtools.ya.test.const
import devtools.ya.test.util.tools
from devtools.ya.test import common as test_common
from devtools.ya.test.system import process
from devtools.ya.test.test_types import common as common_types

GO_TEST_TYPE = "go_test"
GO_BENCH_TEST_TYPE = "go_bench"


class GoSubtestInfo(test_common.SubtestInfo):
    @classmethod
    def from_json(cls, d):
        # type: (dict) -> GoSubtestInfo
        return cls(
            d["test"],
            d.get("subtest", ""),
            skipped=d.get("skipped", False),
            tags=d.get("tags", []),
            path=d.get("path"),
        )

    def __init__(self, test, subtest="", skipped=False, tags=None, path=None):
        # type: (str, str, bool, list, str) -> None
        super(GoSubtestInfo, self).__init__(test, subtest, skipped=skipped, tags=tags)
        self.path = path

    def to_json(self):
        # type: () -> dict
        return {
            "test": self.test,
            "subtest": self.subtest,
            "skipped": self.skipped,
            "tags": getattr(self, "tags", None) or [],
            "nodeid": None,  # Go tests don't have pytest-style nodeid
            "path": getattr(self, "path", None),
            "line": None,  # Go tests don't expose line numbers
            "params": None,  # Go tests don't have parametrization
        }


class GoTestListResult(list):
    def __init__(self, subtests, test_files):
        # type: (list, list) -> None
        super(GoTestListResult, self).__init__(subtests)
        # in golang we can't determine relationship between test name and test file, so we had to
        #  store both test names and test files as separate lists
        self.test_files = test_files


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

    def support_retries(self):
        return True

    @property
    def supports_allure(self):
        return True

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        cmd = (
            devtools.ya.test.util.tools.get_test_tool_cmd(
                opts, 'run_go_test', self.global_resources, wrapper=True, run_on_target_platform=True
            )
            + [
                '--binary',
                self.binary_path('$(BUILD_ROOT)'),
                '--test-work-dir',
                test_work_dir,
                '--tracefile',
                os.path.join(test_work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
                '--modulo',
                str(self._modulo),
                '--modulo-index',
                str(self._modulo_index),
                '--partition-mode',
                self.get_fork_partition_mode(),
                '--output-dir',
                os.path.join(test_work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
                '--project-path',
                self.project_path,
                '--timeout',
                str(self.timeout),
                '--verbose',
            ]
            + (['--go-coverage-per-pkg'] if opts.go_coverage_per_pkg else [])
        )

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

        if devtools.ya.test.const.YaTestTags.GoNoSubtestReport in self.tags:
            cmd.append('--no-subtest-report')

        if devtools.ya.test.const.YaTestTags.GoTotalReport in self.tags:
            cmd.append('--total-report')

        if opts.canonize_tests:
            cmd.append('--report-deselected')

        if getattr(opts, 'allure_report', False):
            cmd.append('--allure')

        return cmd

    def get_type(self):
        return GO_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.REGULAR

    def get_list_cmd(self, arc_root, build_root, opts):
        # type: (str, str, object) -> list
        cmd = self.get_run_cmd(opts) + ['--test-list', '--source-root', arc_root]
        go_tools = self.global_resources[devtools.ya.test.const.GO_TOOLS_RESOURCE]
        cmd += ['--go-tool', os.path.join(go_tools, 'bin', 'go')]

        return cmd

    @classmethod
    def list(cls, cmd, cwd):
        # type: (list, str) -> GoTestListResult
        result = process.execute(cmd, check_exit_code=False, cwd=cwd)
        subtests = cls._get_subtests_info(result)
        test_files = cls._parse_test_files_from_stdout(result.std_out)
        return GoTestListResult(subtests, test_files)

    @classmethod
    def _get_subtests_info(cls, list_cmd_result):
        # type: (object) -> list
        if list_cmd_result.exit_code != 0:
            raise Exception(list_cmd_result.std_err)
        result = []
        for x in list_cmd_result.std_err.split():
            if devtools.ya.test.const.TEST_SUBTEST_SEPARATOR in x:
                testname, subtest = x.split(devtools.ya.test.const.TEST_SUBTEST_SEPARATOR, 1)
                result.append(GoSubtestInfo(testname, subtest))
        return result

    @classmethod
    def _parse_test_files_from_stdout(cls, stdout):
        # type: (str) -> list
        for line in stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                data = json.loads(line)
                if 'test-files' in data:
                    return data['test-files']
            except (ValueError, KeyError):
                continue
        return []

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

    def support_retries(self):
        return True

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        go_bench_timeout = self.meta.go_bench_timeout
        cmd = super(GoBenchSuite, self).get_run_cmd(opts, retry, for_dist_build) + ["--bench-run"]
        if go_bench_timeout:
            cmd += ["--benchmark-timeout", go_bench_timeout]
        return cmd

    def get_type(self):
        return GO_BENCH_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.REGULAR
