# coding: utf-8
import fnmatch
import logging
import os
import sys

import six

import devtools.ya.jbuild.gen.consts as consts

import exts.yjson as json

from devtools.ya.test.system import process
from devtools.ya.test import common as test_common
from devtools.ya.test import facility
from devtools.ya.test.test_types import common
import devtools.ya.core.config
import devtools.ya.test.const
import devtools.ya.test.util.tools
import devtools.ya.test.test_node.cmdline as cmdline

logger = logging.getLogger(__name__)


PYTEST_TYPE = "pytest"
EXEC_TEST_TYPE = "exectest"
IMPORT_TEST_TYPE = "import_test"
GOFMT_TEST_TYPE = "gofmt"
GOVET_TEST_TYPE = "govet"
CLASSPATH_CLASH_TYPE = "classpath.clash"


class PytestSubtestInfo(test_common.SubtestInfo):
    @classmethod
    def from_json(cls, d):
        return cls(d["test"], d.get("subtest", ""), skipped=d.get("skipped", False), tags=d.get("tags", []))

    def __init__(
        self,
        test,
        subtest="",
        skipped=False,
        tags=None,
        nodeid=None,
        path=None,
        line=None,
        params=None,
        pytest_class=None,
    ):
        self.test = test
        self.subtest = test_common.normalize_utf8(subtest)
        self.skipped = skipped
        self.tags = tags
        self.nodeid = nodeid
        self.path = path
        self.line = line
        self.params = params
        self.pytest_class = pytest_class

    def to_json(self):
        return {
            "test": self.test,
            "subtest": self.subtest,
            "skipped": self.skipped,
            "tags": getattr(self, "tags", []),
            "nodeid": getattr(self, "nodeid"),
            "path": getattr(self, "path"),
            "line": getattr(self, "line"),
            "params": getattr(self, "params"),
        }


class PyTestSuite(common.PythonTestSuite):
    """
    Support for pytest framework http://pytest.org/
    """

    def __init__(self, meta, *args, **kwargs):
        super(PyTestSuite, self).__init__(meta, *args, **kwargs)
        if self.meta.source_folder_path is not None:
            self.pytest_output_dir_deps = {self.meta.source_folder_path}
        else:
            self.pytest_output_dir_deps = set()

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

    def get_test_files(self, opts=None):
        test_files = sorted(set(super(PyTestSuite, self).get_test_files(opts)))

        if opts is not None and opts.canonize_tests:
            return test_files

        tests_filters = getattr(opts, "tests_filters", []) + self.get_additional_filters()
        if not tests_filters:
            return test_files
        name_filter = make_py_file_filter(tests_filters)
        files = list(filter(name_filter, test_files))
        return files

    def setup_dependencies(self, graph):
        super(PyTestSuite, self).setup_dependencies(graph)
        seen = set()

        for dep in self.get_build_dep_uids():
            for uid, outputs in graph.get_target_outputs_by_type(dep, module_types=['so'], unroll_bundle=True):
                if uid in seen:
                    continue
                seen.add(uid)

                for o in outputs:
                    if not os.path.splitext(o)[1] in devtools.ya.test.const.FAKE_OUTPUT_EXTS:
                        self.pytest_output_dir_deps.add(os.path.dirname(o))

    def get_type(self):
        return PYTEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.REGULAR

    @staticmethod
    def _get_ini_file_path():
        return "pkg:library.python.pytest:pytest.yatest.ini"

    def get_work_dir(self, retry, remove_tos=False):
        return test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            split_file=self._split_file_name,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=remove_tos,
        )

    def get_run_cmd_args(self, opts, retry, for_dist_build):
        work_dir = self.get_work_dir(retry, opts.remove_tos)
        output_dir = os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME)
        cmd = [
            "--basetemp",
            os.path.join('$(BUILD_ROOT)', "tmp"),
            "--capture",
            "no",
            # avoid loading custom pytest.ini files for now
            "-c",
            self._get_ini_file_path(),
            "-p",
            "no:factor",
            "--doctest-modules",
            "--ya-trace",
            os.path.join(work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            "--build-root",
            '$(BUILD_ROOT)',
            "--source-root",
            "$(SOURCE_ROOT)",
            "--output-dir",
            output_dir,
            "--durations",
            "0",
            "--project-path",
            self.project_path,
            "--test-tool-bin",
            devtools.ya.test.util.tools.get_test_tool_path(
                opts, self.global_resources, devtools.ya.test.const.TEST_TOOL_TARGET in self.global_resources
            ),
            # XXX
            # version allows library/python/pytest/plugins/ya.py stay compatible with ya and ya-dev
            # if changes in ya-dev are not backward-compatible (like changes in tracefile format)
            "--ya-version",
            "2",
            # "--valgrind-path", os.path.join("$(VALGRIND)", "valgrind", "valgrind"),
        ]

        if not for_dist_build and getattr(opts, 'collect_cores', False):
            cmd += ["--collect-cores"]

        if opts and getattr(opts, "sanitize", None):
            cmd += ["--sanitizer-extra-checks"]

        if opts and hasattr(opts, "build_type"):
            cmd += ["--build-type", opts.build_type]

        if opts and hasattr(opts, "test_stderr") and opts.test_stderr:
            cmd += ["--test-stderr"]

        if opts and hasattr(opts, "test_log_level") and opts.test_log_level:
            cmd += ["--test-log-level", opts.test_log_level]

        if opts and hasattr(opts, "tests_filters") and opts.tests_filters:
            for flt in opts.tests_filters:
                if flt.startswith("mark:"):
                    cmd += ["-m", flt[len("mark:") :]]
                else:
                    cmd += ["--test-filter", flt]

        for flt in self._additional_filters:
            if flt.startswith("mark:"):
                cmd += ["-m", flt[len("mark:") :]]
            else:
                cmd += ["--test-filter", flt]

        if opts and getattr(opts, "test_traceback", None):
            cmd += ["--tb", getattr(opts, "test_traceback")]

        if opts and getattr(opts, "pdb", None):
            cmd += ["--pdb", "--pdbcls", "IPython.terminal.debugger:TerminalPdb"]

        if self.supports_allure and opts and getattr(opts, "allure_report"):
            cmd += ["--alluredir", os.path.join(work_dir, "allure")]
            if not self._use_arcadia_python:
                logger.warning("allure report may not be built if any of allure dependencies is not installed")

        if self._modulo > 1:
            cmd += [
                "--modulo",
                str(self._modulo),
                "--modulo-index",
                str(self._modulo_index),
                "--partition-mode",
                self.get_fork_partition_mode(),
            ]
        if self.get_fork_mode() == "subtests":
            cmd.append("--split-by-tests")

        for dirname in sorted(self.pytest_output_dir_deps):
            cmd += ["--dep-root", dirname]

        if opts and (getattr(opts, 'show_deselected_tests') or getattr(opts, 'canonize_tests')):
            cmd += ["--report-deselected"]

        if opts and getattr(opts, 'test_debug'):
            cmd += ["--test-debug", "--pdb-on-sigusr1"]

        if opts and getattr(opts, 'flags'):
            for key in sorted(opts.flags.keys()):
                if key in [
                    'RECURSE_PARTITION_INDEX',
                    'RECURSE_PARTITIONS_COUNT',
                    'CONSISTENT_DEBUG',
                    'CONSISTENT_DEBUG_LIGHT',
                ]:
                    continue
                cmd += ['--flags', "{}={}".format(key, opts.flags[key])]

        sanitizer_name = opts.sanitize or opts.flags.get('SANITIZER_TYPE')
        if sanitizer_name:
            cmd += ['--sanitize', sanitizer_name]

        if opts and getattr(opts, 'profile_pytest'):
            cmd += ["--profile-pytest"]

        if opts and getattr(opts, 'fail_fast'):
            cmd += ["--exitfirst"]

        if opts and getattr(opts, 'pytest_args') and opts.pytest_args:
            cmd += opts.pytest_args

        return cmd

    def get_list_cmd(self, arc_root, build_root, opts):
        # -qqq gives the needed for parsing test list output
        return self.get_run_cmd(opts) + ["--collect-only", "--mode", "list", "-qqq"]

    @classmethod
    def list(cls, cmd, cwd):
        return cls._get_subtests_info(process.execute(cmd, check_exit_code=False))

    @classmethod
    def _get_subtests_info(cls, list_cmd_result):
        result = []
        try:
            # stderr may contain warnings from modules under test,
            # but tests list will be on the last line.
            tests_line = list_cmd_result.std_err.splitlines()[-1]
            tests = json.loads(tests_line)
            for t in tests:
                result.append(
                    test_common.SubtestInfo(
                        test_common.strings_to_utf8(t["class"]),
                        test_common.strings_to_utf8(t["test"]),
                        tags=t.get("tags", []),
                    )
                )
            return result
        except Exception as e:
            ei = sys.exc_info()
            six.reraise(
                ei[0],
                "{}\nListing output: {}".format(str(e), list_cmd_result.std_err or list_cmd_result.std_out),
                ei[2],
            )

    @property
    def setup_pythonpath_env(self):
        # XXX remove when YA-1008 is done
        return 'ya:no_pythonpath_env' not in self.tags

    @property
    def supports_allure(self):
        return True

    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]


class PyTestBinSuite(PyTestSuite):
    def __init__(self, meta, *args, **kwargs):
        super(PyTestBinSuite, self).__init__(meta, *args, **kwargs)
        if self.meta.binary_path:
            # FIXME: Meta must be read only
            self.meta = facility.meta_dart_replace_test_name(self.meta, self.get_type())

    def binary_path(self, root):
        return common.AbstractTestSuite.binary_path(self, root)

    def get_run_cmd(self, opts, retry=None, for_dist_build=False, for_listing=False):
        cmd = [
            self.binary_path('$(BUILD_ROOT)'),
        ] + self.get_run_cmd_args(opts, retry, for_dist_build)

        if not for_listing and self.get_parallel_tests_within_node_workers():
            if self.get_parallel_tests_within_node_workers() > 1:
                self.logger.debug("Enable parralel test in node")
                test_tool = devtools.ya.test.util.tools.get_test_tool_cmd(
                    opts, 'run_pytest', self.global_resources, wrapper=True, run_on_target_platform=True
                )
                cmd = test_tool + ["--binary"] + cmd
                cmd += ["--worker-count", str(self.get_parallel_tests_within_node_workers())]
                cmd += ["--temp-tracefile-dir", self.get_temp_tracefile_dir(retry, opts.remove_tos)]

        if self._split_file_name:
            cmd += ["--test-file-filter", self._split_file_name]

        if self.wine_path:
            cmd = [self.wine_path] + cmd + ["--assert=plain"]

        runner_bin = self.meta.test_runner_bin

        if runner_bin is not None:
            cmd = [runner_bin] + cmd

        return cmd

    def setup_environment(self, env, opts):
        """
        setup environment for running test command
        """
        super(PyTestBinSuite, self).setup_environment(env, opts)
        if opts.detect_leaks_in_pytest:
            for envvar in ['LSAN_OPTIONS', 'ASAN_OPTIONS']:
                original_opts = env.get(envvar)
                env.extend_mandatory(envvar, "detect_leaks=0")
                if original_opts:
                    env[envvar + "_ORIGINAL"] = original_opts

        if opts.external_py_files:
            # Explicitly specify PYTHONPYCACHEPREFIX for tests.
            # This is the only way to set the correct path to the pycache directory in the case
            # where the executable is run with the current working directory inside arcadia
            # (e.g. the ya:dirty case or the direct cwd=arcadia case).
            env["PYTHONPYCACHEPREFIX"] = devtools.ya.core.config.pycache_path()
        else:
            # pytest installs own import hook to overwrite asserts - AssertionRewritingHook
            # Tests can import modules specified in the DATA which will generate patched pyc-files.
            # We are setting PYTHONDONTWRITEBYTECODE=1 to prevent this behaviour by default.
            env["PYTHONDONTWRITEBYTECODE"] = "1"

    @property
    def supports_fork_test_files(self):
        return True

    @property
    def supports_allure(self):
        return True

    @property
    def supports_coverage(self):
        return True

    def get_type(self):
        return PYTEST_TYPE

    @classmethod
    def list(cls, cmd, cwd):
        filename = os.path.join(cwd, 'test_list.json')
        res = process.execute(cmd + ["--test-list-file", filename], check_exit_code=True, cwd=cwd)
        return cls._get_subtests_info(res, filename)

    def get_list_cmd(self, arc_root, build_root, opts):
        # -qqq gives the needed for parsing test list output
        return self.get_run_cmd(opts, for_listing=True) + ["--collect-only", "--mode", "list", "-qqq"]

    @classmethod
    def _get_subtests_info(cls, list_cmd_result, filename):
        result = []
        try:
            with open(filename) as afile:
                tests = json.load(afile)
            for t in tests:
                result.append(
                    PytestSubtestInfo(
                        test=test_common.strings_to_utf8(t["class"]),
                        subtest=test_common.strings_to_utf8(t["test"]),
                        tags=test_common.strings_to_utf8(t.get("tags", [])),
                        nodeid=test_common.strings_to_utf8(t.get("nodeid")),
                        path=test_common.strings_to_utf8(t.get("path")),
                        line=test_common.strings_to_utf8(t.get("line")),
                        params=test_common.strings_to_utf8(t.get("params")),
                        pytest_class=test_common.strings_to_utf8(t.get("pytest_class")),
                    )
                )
            return result
        except Exception as e:
            ei = sys.exc_info()
            six.reraise(
                ei[0],
                "{}\nListing output: {}".format(str(e), list_cmd_result.std_err or list_cmd_result.std_out),
                ei[2],
            )

    def get_temp_tracefile_dir(self, retry, remove_tos):
        # Slightly trimmed call for now.
        test_work_dir = self.get_work_dir(retry, remove_tos=remove_tos)
        return os.path.join(test_work_dir, devtools.ya.test.const.TEMPORARY_TRACE_DIR_NAME)


class Py3TestBinSuite(PyTestBinSuite):
    def get_type(self):
        return "py3test"

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.REGULAR


class ExecTest(PyTestBinSuite):
    def get_type(self):
        return EXEC_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.REGULAR

    @property
    def name(self):
        return EXEC_TEST_TYPE

    @property
    def supports_fork_test_files(self):
        return False

    def setup_environment(self, env, opts):
        """
        setup environment for running test command
        """
        super(ExecTest, self).setup_environment(env, opts)
        dep_dirs = {"$(BUILD_ROOT)/{}".format(os.path.dirname(dep)) for dep in self._custom_dependencies}
        env["PATH"] = os.path.pathsep.join(sorted(dep_dirs))

    def get_run_cmd(self, opts, retry=None, for_dist_build=False, for_listing=False):
        cmd = ["--test-param", "commands={}".format(self.meta.blob)]
        cmd += devtools.ya.test.util.tools.get_test_tool_cmd(
            opts, 'run_exectest', self.global_resources, wrapper=True, run_on_target_platform=True
        )
        cmd += self.get_run_cmd_args(opts, retry, for_dist_build)
        cmd += ["--noconftest"]
        return cmd

    @property
    def supports_allure(self):
        return False


class LintTestSuite(common.StyleTestSuite):
    def get_suite_files(self):
        return sorted(f.replace("$S", "$(SOURCE_ROOT)") for f in self.meta.test_files if f)

    def support_retries(self):
        return False

    @classmethod
    def get_ci_type_name(cls):
        return "style"

    def get_arcadia_test_data(self):
        # XXX validate resource checks uses files section to specify resource id,
        # which is obvious not a file dependency.
        return [f.replace("$(SOURCE_ROOT)/", "") for f in self._files if f.startswith("$(SOURCE_ROOT)/")]

    @property
    def supports_canonization(self):
        return False

    def _get_files(self, opts=None):
        return self._files

    def get_run_cmd_inputs(self, opts):
        inputs = super(LintTestSuite, self).get_run_cmd_inputs(opts)
        return inputs + self._get_files(opts)

    def get_list_cmd(self, arc_root, build_root, opts):
        cmd = self.get_run_cmd(opts) + ["--list"]
        return cmd

    @classmethod
    def list(cls, cmd, cwd):
        result = []
        list_cmd_result = process.execute(cmd, cwd=cwd)
        if list_cmd_result.exit_code == 0:
            for line in [_f for _f in [line.strip() for line in list_cmd_result.std_out.split(os.linesep)] if _f]:
                result.append(test_common.SubtestInfo(line, cls.__name__))
            return result
        raise Exception(list_cmd_result.std_err)


class PyLintTestSuite(LintTestSuite):
    def __init__(
        self, meta, modulo=1, modulo_index=0, target_platform_descriptor=None, multi_target_platform_run=False
    ):
        super(PyLintTestSuite, self).__init__(
            meta,
            modulo,
            modulo_index,
            target_platform_descriptor,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._files = self.get_suite_files()

    def _add_checker_stderr(self):
        return True

    def _add_checker_stdout(self):
        return True

    @classmethod
    def is_batch(cls):
        return False

    def batch_name(self):
        raise NotImplementedError()

    def get_checker(self, opts, for_dist_build, out_path):
        raise NotImplementedError()

    def get_run_cmd(self, opts, retry=None, for_dist_build=False):
        work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            retry,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        out_path = os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME)

        cmd = (
            devtools.ya.test.util.tools.get_test_tool_cmd(
                opts, 'run_check', self.global_resources, wrapper=True, run_on_target_platform=True
            )
            + [
                "--source-root",
                "$(SOURCE_ROOT)",
                "--checker",
                self.get_checker(opts, for_dist_build, out_path),
                "--check-name",
                self.get_type(),
                "--trace-path",
                os.path.join(work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
                "--out-path",
                out_path,
                "--log-path",
                os.path.join(out_path, "run_check.log"),
            ]
            + self._get_files(opts)
        )
        if not self._add_checker_stderr():
            cmd += ["--no-snippet-from-stderr"]
        if not self._add_checker_stdout():
            cmd += ["--no-snippet-from-stdout"]
        if self.is_batch():
            cmd += ["--batch", "--batch-name", self.batch_name()]
        if self._check_file_pattern():
            cmd += ["--file-pattern", self._check_file_pattern()]
        for f in opts.tests_filters + self._additional_filters:
            cmd += ["--tests-filters", f]
        return cmd

    def _check_file_pattern(self):
        return None

    def get_computed_test_names(self, opts):
        typename = self.get_type()
        return ["{}::{}".format(os.path.basename(filename), typename) for filename in self._get_files()]

    @property
    def setup_pythonpath_env(self):
        # XXX remove when YA-1008 is done
        return 'ya:no_pythonpath_env' not in self.tags


class CheckImportsTestSuite(PyLintTestSuite):
    @classmethod
    def get_ci_type_name(cls):
        return "test"

    def get_checker(self, opts, dist_build, out_path):
        cmd = 'run_pyimports --markup'

        skips_list = self.meta.no_check
        if skips_list:
            for s in skips_list:
                cmd += " --skip {}".format(s)

        cmd += " --trace-dir={}".format(out_path)

        return cmd

    def _get_files(self, opts=None):
        files = super(CheckImportsTestSuite, self)._get_files(opts)
        return [os.path.join("$(BUILD_ROOT)", f) for f in files]

    # Don't use style files as test data dependency
    def get_arcadia_test_data(self):
        return super(common.StyleTestSuite, self).get_arcadia_test_data()

    def _add_checker_stdout(self):
        return False

    def get_type(self):
        return IMPORT_TEST_TYPE

    @property
    def name(self):
        return IMPORT_TEST_TYPE

    def setup_environment(self, env, opts):
        """
        setup environment for running test command
        """
        env.extend_mandatory("LSAN_OPTIONS", "detect_leaks=0")

    @property
    def setup_pythonpath_env(self):
        return False

    def support_retries(self):
        return False

    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]


class GoFmtTestSuite(PyLintTestSuite):
    def get_type(self):
        return GOFMT_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.STYLE

    def get_test_dependencies(self):
        return []

    def get_run_cmd(self, opts, retry=None, for_dist_build=False):
        work_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            self.project_path,
            self.name,
            retry,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        cmd = (
            devtools.ya.test.util.tools.get_test_tool_cmd(
                opts, 'run_go_fmt', self.global_resources, wrapper=True, run_on_target_platform=True
            )
            + [
                "--gofmt",
                '{}/bin/gofmt'.format(
                    self.global_resources.get(
                        devtools.ya.test.const.GO_TOOLS_RESOURCE, devtools.ya.test.const.GO_TOOLS_RESOURCE
                    )
                ),
                "--source-root",
                "$(SOURCE_ROOT)",
                "--trace-path",
                os.path.join(work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
                "--out-path",
                os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
            ]
            + self._get_files(opts)
        )
        for f in opts.tests_filters + self._additional_filters:
            cmd += ["--tests-filters", f]
        return cmd

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default
        return True

    def _add_checker_stdout(self):
        return False

    @property
    def setup_pythonpath_env(self):
        return False


class GoVetTestSuite(PyLintTestSuite):
    def get_type(self):
        return GOVET_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.STYLE

    def get_test_dependencies(self):
        return [self.project_path]

    def get_checker(self, opts, dist_build, out_path):
        return 'run_go_vet'

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default
        return True

    def _add_checker_stdout(self):
        return False

    @property
    def setup_pythonpath_env(self):
        return False


class ClasspathClashTestSuite(PyLintTestSuite):
    def __init__(self, meta, target_platform_descriptor=None, multi_target_platform_run=False):
        super(ClasspathClashTestSuite, self).__init__(
            meta,
            target_platform_descriptor=target_platform_descriptor,
            multi_target_platform_run=multi_target_platform_run,
        )
        self.test_name = self.meta.test_name
        self.ignored = sorted({('ignore_class:' + i) for i in self.meta.ignore_classpath_clash.split(" ") if i})
        self.strict = self.meta.strict_classpath_clash is not None

        paths = [
            item[len(consts.BUILD_ROOT_SHORT) + 1 :] if item.startswith(consts.BUILD_ROOT_SHORT) else item
            for item in self.meta.classpath.split()
        ]

        self.classpath = ["{}/{}".format(consts.BUILD_ROOT, p) for p in paths]
        self.deps = sorted(set(paths))

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.STYLE

    def get_type(self):
        return CLASSPATH_CLASH_TYPE

    @property
    def name(self):
        return self.test_name

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default
        return True

    def get_test_dependencies(self):
        return [_f for _f in self.deps if _f]

    def get_checker(self, opts, dist_build, out_path):
        return "run_classpath_clash"

    def get_run_cmd(self, opts, retry=None, for_dist_build=False):
        cmd = super(ClasspathClashTestSuite, self).get_run_cmd(opts, retry, for_dist_build)
        cmd.append(consts.BUILD_ROOT)
        cmd += cmdline.wrap_with_cmd_file_markers([x[len(consts.BUILD_ROOT) + 1 :] for x in self.classpath])
        cmd += self.ignored
        if for_dist_build or self.strict:
            cmd.append('--')
        if for_dist_build:
            cmd.append("--verbose")
        if self.strict:
            cmd.append("--strict")
        return cmd

    @classmethod
    def is_batch(cls):
        return True

    def batch_name(self):
        return self.test_name

    def _add_checker_stderr(self):
        return False


# XXX move out
def make_py_file_filter(filter_names):
    file_filters = []
    for flt in filter_names:
        flt = flt.split(':')[0].replace('.', '?')
        if '*' in flt:
            flt = flt.split('*')[0] + '*'
        file_filters.append(flt)

    def predicate(filename):
        return any([fnmatch.fnmatch(filename, filter_name) for filter_name in file_filters])

    return predicate
