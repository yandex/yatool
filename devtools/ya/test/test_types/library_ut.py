import os
import json
import six
import logging
from typing import List  # noqa: F401
from typing import Optional  # noqa: F401
from typing import Union  # noqa: F401

import exts.windows
import devtools.ya.test.common as test_common
import devtools.ya.test.const
from devtools.ya.test.system import process
from devtools.ya.test.test_types import common as common_types
from devtools.ya.test.util import tools

logger = logging.getLogger(__name__)

UNITTEST_TYPE = "unittest"


class CppSubtestInfo(test_common.SubtestInfo):
    @classmethod
    def from_json(cls, json_str):
        # type: (str) -> CppSubtestInfo
        test_obj = json.loads(json_str)
        nodeid = test_obj['nodeid']
        if devtools.ya.test.const.TEST_SUBTEST_SEPARATOR in nodeid:
            test_obj['test'], test_obj['subtest'] = nodeid.split(devtools.ya.test.const.TEST_SUBTEST_SEPARATOR, 1)
        else:
            test_obj['test'], test_obj['subtest'] = nodeid, ""
        return cls.from_dict(test_obj)

    @classmethod
    def from_dict(cls, data):
        # type: (dict) -> CppSubtestInfo
        return cls(
            data["test"],
            data.get("subtest", ""),
            skipped=data.get("skipped", False),
            tags=data.get("tags", []),
            file=data.get("file"),
            line=data.get("line"),
            nodeid=data.get("nodeid"),
            name=data.get("name"),
            test_suite_name=data.get("test_suite_name"),
        )

    def __init__(
        self,
        test,  # type: str
        subtest="",  # type: str
        skipped=False,  # type: bool
        tags=None,  # type: Optional[List[str]]
        file=None,  # type: Optional[str]
        line=None,  # type: Optional[int]
        nodeid=None,  # type: Optional[str]
        name=None,  # type: Optional[str]
        test_suite_name=None,  # type: Optional[str]
    ):
        # type: (...) -> None
        super(CppSubtestInfo, self).__init__(test, subtest, skipped, tags=tags)
        self.file = file
        self.line = line
        self.nodeid = nodeid
        self.name = name
        self.test_suite_name = test_suite_name

    def to_json(self):
        result = super(CppSubtestInfo, self).to_json()
        result["file"] = self.file
        result["line"] = self.line
        result["nodeid"] = self.nodeid
        result["name"] = self.name
        result["test_suite_name"] = self.test_suite_name
        return result


class UnitTestSuite(common_types.AbstractTestSuite):
    def support_splitting(self, opts=None):
        """
        Does test suite support splitting
        """
        return True

    def support_list_node(self):
        """
        Does test suite support list_node before test run
        """
        return True

    def support_retries(self):
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
        cmd = tools.get_test_tool_cmd(
            opts, 'run_ut', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            '--binary',
            self.binary_path('$(BUILD_ROOT)'),
            '--trace-path',
            os.path.join(test_work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            '--output-dir',
            os.path.join(test_work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
            '--modulo',
            str(self._modulo),
            '--modulo-index',
            str(self._modulo_index),
            '--partition-mode',
            self.get_fork_partition_mode(),
            '--project-path',
            self.project_path,
            # remove hardcoded restriction on obtaining list of the tests
            '--list-timeout',
            str(int(self.timeout * 0.5)),
            '--verbose',
        ]

        if not exts.windows.on_win():
            cmd += ["--gdb-path", os.path.join("$(GDB)", "gdb", "bin", "gdb")]

        if devtools.ya.test.const.YaTestTags.SequentialRun in self.tags:
            cmd.append("--sequential-launch")

        if getattr(opts, 'gdb', False) and not for_dist_build:
            cmd.append("--gdb-debug")

        if getattr(opts, 'clang_coverage'):
            import signal

            cmd += ['--stop-signal', str(int(signal.SIGUSR2))]

        build_type = getattr(opts, 'build_type') or ''
        if 'valgrind' in build_type:
            cmd += ['--valgrind-path', os.path.join("$(VALGRIND)", "valgrind")]

        if self.wine_path:
            cmd += ["--with-wine", "--wine-path", self.wine_path]

        if for_dist_build and not getattr(opts, 'keep_full_test_logs', False):
            cmd.append("--truncate-logs")

        if self.get_fork_mode() == "subtests":
            cmd.append("--split-by-tests")

        if not getattr(opts, 'collect_cores', False):
            cmd.append('--dont-store-cores')

        if opts and hasattr(opts, "tests_filters") and opts.tests_filters:
            for flt in opts.tests_filters:
                cmd += ['--test-filter', flt]

        for flt in self._additional_filters:
            cmd += ['--test-filter', flt]

        if opts and hasattr(opts, "test_params") and opts.test_params:
            for key, value in six.iteritems(opts.test_params):
                cmd += ["--test-param", "{}={}".format(key, value)]

        if opts.test_binary_args:  # some tests use FakeOpts where this option is None
            for additional_arg in opts.test_binary_args:
                cmd += ["--test-binary-args={}".format(additional_arg)]

        if self.get_parallel_tests_within_node_workers():
            if not for_dist_build:
                cmd += ["--parallel-tests-within-node-workers", str(self.get_parallel_tests_within_node_workers())]
                cmd += ["--temp-tracefile-dir", self.temp_tracefile_dir]
            else:
                logger.info("Parallel tests execution is not supported for dist build")

        return cmd

    def get_type(self):
        return UNITTEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.REGULAR

    # TODO remove property when dartinfo's TEST-NAME will be 'unittest' for library_ut
    @property
    def name(self):
        return UNITTEST_TYPE

    def get_list_cmd(self, arc_root, build_root, opts):
        test_work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
        )

        cmd = tools.get_test_tool_cmd(
            opts, 'run_ut', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            '--binary',
            self.binary_path('$(BUILD_ROOT)'),
            '--test-list',
            # don't allow to hang up the node that displays the list of tests
            '--list-timeout',
            str(min(40, int(self.timeout * 0.5))),
            '--project-path',
            self.project_path,
            '--trace-path',
            os.path.join(test_work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
        ]
        if opts and hasattr(opts, "tests_filters") and opts.tests_filters:
            for flt in opts.tests_filters:
                cmd += ['--test-filter', flt]
        return cmd

    @classmethod
    def list(cls, cmd, cwd):
        return cls._get_cpp_subtests_info(process.execute(cmd, check_exit_code=False, cwd=cwd))

    @staticmethod
    def _get_cpp_subtests_info(cmd_result):
        result = []  # type: List[Union[CppSubtestInfo, test_common.SubtestInfo]]
        if cmd_result.exit_code == 0:
            for line in cmd_result.std_out.splitlines():
                line = line.strip()
                if not line:
                    continue

                # processing new detailed JSON tests info format, for example:
                #  {"file":"path/to/test.cpp","name":"Test","test_suite_name":"TestGroup","nodeid":"TestGroup::Test","line":3}
                try:
                    result.append(CppSubtestInfo.from_json(line))
                    continue
                except (ValueError, KeyError) as exc:
                    logger.debug('Failed to add test list info due to error: %s', exc)
                    pass

                # Fallback to old format, "suite::test" string (without extra fields)
                if devtools.ya.test.const.TEST_SUBTEST_SEPARATOR in line:
                    test, subtest = line.split(devtools.ya.test.const.TEST_SUBTEST_SEPARATOR, 1)
                    result.append(test_common.SubtestInfo(test, subtest))

            return result
        raise Exception(cmd_result.std_err)

    @property
    def supports_canonization(self):
        return False

    @property
    def supports_test_parameters(self):
        return True

    @property
    def supports_allure(self):
        return True

    @property
    def supports_coverage(self):
        return True

    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]

    @property
    def temp_tracefile_dir(self):
        test_work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
        )
        return os.path.join(test_work_dir, devtools.ya.test.const.TEMPORARY_TRACE_DIR_NAME)
