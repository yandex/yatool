import os
import logging

import devtools.ya.test.test_types.common as test_types
import devtools.ya.test.util.tools as test_tools
import devtools.ya.test.common
import devtools.ya.test.const
import devtools.ya.test.system.process

from devtools.ya.jbuild.gen import base
from devtools.ya.test import common as test_common
from devtools.ya.test.system import process
from devtools.ya.test.test_types.py_test import LintTestSuite

import yalibrary.graph.base as graph_base
from yalibrary.graph.const import BUILD_ROOT, SOURCE_ROOT

logger = logging.getLogger(__name__)


KTLINT_TEST_TYPE = "ktlint"


class JavaStyleTestSuite(test_types.StyleTestSuite):
    JSTYLE_MIGRATIONS_FILE = "build/rules/jstyle/migrations.yaml"

    def __init__(self, meta, target_platform_descriptor=None, multi_target_platform_run=False):
        super(JavaStyleTestSuite, self).__init__(
            meta,
            target_platform_descriptor=target_platform_descriptor,
            multi_target_platform_run=multi_target_platform_run,
        )
        self.initialized = False
        self.deps = None
        self.my_sources = []
        self.config_xml = ''
        for f in self.meta.files:
            if '.srclst::' in f:
                self.my_sources.append(f)
            else:
                self.config_xml = f
        self.my_index_files = []
        if not self.config_xml.startswith('/'):
            self.config_xml = graph_base.hacked_path_join(SOURCE_ROOT, self.project_path, self.config_xml)
        self.jdk_resource = None
        self.jstyle_runner_path = None

    def init_from_opts(self, opts):
        self.jdk_resource = base.resolve_jdk(self.global_resources, jdk_version=self.latest_jdk_version, opts=opts)
        self.jstyle_runner_path = base.resolve_jstyle_lib(self.global_resources, opts)
        self.my_index_files = [graph_base.hacked_normpath(i.split('::', 2)[0]) for i in self.my_sources]
        self.deps = self.my_index_files

    @property
    def latest_jdk_version(self):
        return self.meta.jdk_latest_version

    def support_splitting(self, opts=None):
        return False

    def get_type(self):
        return "java.style"

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.STYLE

    @property
    def cache_test_results(self):
        # suite is considered to be steady
        return True

    def get_test_dependencies(self):
        return [_f for _f in set(self.deps) if _f]

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        work_dir = devtools.ya.test.common.get_test_suite_work_dir(
            BUILD_ROOT,
            self.project_path,
            self.name,
            retry=retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        cmd = test_tools.get_test_tool_cmd(
            opts, 'run_javastyle', self.global_resources, wrapper=True, run_on_target_platform=False
        ) + [
            '--source-root',
            SOURCE_ROOT,
            '--trace-path',
            os.path.join(work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            '--out-path',
            os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
            '--java',
            test_tools.jdk_tool('java', jdk_path=self.jdk_resource),
            '--runner-lib-path',
            self.jstyle_runner_path,
            '--config-xml',
            self.config_xml,
            '--verbose',
            '--modulo',
            str(self._modulo),
            '--modulo-index',
            str(self._modulo_index),
        ]
        if opts and getattr(opts, 'use_jstyle_server'):
            cmd += ["--use-jstyle-server"]

        if not (opts and getattr(opts, 'disable_jstyle_migrations')):
            cmd += ["--jstyle-migrations", os.path.join(SOURCE_ROOT, self.JSTYLE_MIGRATIONS_FILE)]

        for f in opts.tests_filters + self._additional_filters:
            cmd += ["--tests-filters", f]
        cmd += self.my_sources
        return cmd

    def get_run_cmd_inputs(self, opts):
        return (
            super(JavaStyleTestSuite, self).get_run_cmd_inputs(opts)
            + self.my_index_files
            + ([self.config_xml] if self.config_xml.startswith(SOURCE_ROOT) else [])
        )

    def get_list_cmd_inputs(self, opts):
        return super(JavaStyleTestSuite, self).get_list_cmd_inputs(opts) + self.my_index_files

    def get_list_cmd(self, arc_root, build_root, opts):
        return self.get_run_cmd(opts) + ["--list"]

    @classmethod
    def list(cls, cmd, cwd):
        result = []
        list_cmd_result = devtools.ya.test.system.process.execute(cmd, cwd=cwd)
        if list_cmd_result.exit_code == 0:
            for line in [_f for _f in [line.strip() for line in list_cmd_result.std_out.split(os.linesep)] if _f]:
                result.append(devtools.ya.test.common.SubtestInfo(line, cls.__name__))
            return result
        raise Exception(list_cmd_result.std_err)

    @property
    def supports_canonization(self):
        return False

    def support_retries(self):
        return False

    @classmethod
    def get_ci_type_name(cls):
        return "style"

    def _get_all_test_data(self, data_type):
        res = super(JavaStyleTestSuite, self)._get_all_test_data(data_type)
        my_dirs = list(map(graph_base.hacked_normpath, [j.split('::', 2)[1] for j in self.my_sources if '::' in j]))
        if data_type == 'file':
            return res + [i.replace(SOURCE_ROOT, 'arcadia') for i in my_dirs if i.startswith(SOURCE_ROOT)]
        return res

    def get_test_related_paths(self, arc_root, opts):
        paths = super(JavaStyleTestSuite, self).get_test_related_paths(arc_root, opts)
        paths.append(os.path.join(arc_root, self.JSTYLE_MIGRATIONS_FILE))
        return paths

    @property
    def env(self):
        env = super(JavaStyleTestSuite, self).env or []
        env.append("YA_CORE_TMP_PATH=$(RESOURCE_ROOT)/tests/jstyle_server")
        return env


class KtlintTestSuite(LintTestSuite):
    def __init__(
        self, meta, modulo=1, modulo_index=0, target_platform_descriptor=None, multi_target_platform_run=False
    ):
        super(KtlintTestSuite, self).__init__(
            meta,
            modulo,
            modulo_index,
            target_platform_descriptor,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._files = self.get_suite_files()

    @property
    def supports_test_parameters(self):
        return False

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default
        return True

    def get_arcadia_test_data(self):
        arcadia_data = super(KtlintTestSuite, self)._get_all_test_data("arcadia")
        srcs_data = super(KtlintTestSuite, self).get_arcadia_test_data()
        return srcs_data + arcadia_data

    def get_test_related_paths(self, arc_root, opts):
        return [self._source_folder_path(arc_root), os.path.join(arc_root, ".editorconfig")]

    def _ktlint_folder_name(self):
        if self.meta.use_ktlint_old is not None and self.meta.use_ktlint_old == 'yes':
            return "ktlint_old"
        return "ktlint"

    def _ktlint_baseline_path(self):
        if self.meta.ktlint_baseline_file is not None:
            return os.path.join(SOURCE_ROOT, self.project_path, self.meta.ktlint_baseline_file)
        return None

    def _ktlint_ruleset(self):
        ruleset = self.meta.ktlint_ruleset
        if ruleset is None:
            return None
        deps = self.meta.custom_dependencies
        if not deps:
            return None
        for dep in deps.split(" "):
            if dep.startswith(ruleset):
                return dep
        return None

    def get_resource_tools(self):
        return (self._ktlint_folder_name(),)

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
        editorconfig = os.path.join(SOURCE_ROOT, "build/platform/java", self._ktlint_folder_name(), ".editorconfig")
        cmd = devtools.ya.test.util.tools.get_test_tool_cmd(
            opts, 'run_ktlint_test', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            "--srclist-path",
            self._get_files()[0],
            "--binary",
            self.meta.ktlint_binary,
            "--trace-path",
            os.path.join(work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            "--source-root",
            SOURCE_ROOT,
            "--build-root",
            BUILD_ROOT,
            "--project-path",
            self.project_path,
            "--output-dir",
            os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
            "--editorconfig",
            editorconfig,
        ]

        baseline = self._ktlint_baseline_path()
        if baseline:
            cmd += ["--baseline", baseline]

        ruleset = self._ktlint_ruleset()
        if ruleset:
            cmd += ["--ruleset", ruleset]

        if opts and hasattr(opts, "tests_filters") and opts.tests_filters:
            for flt in opts.tests_filters:
                cmd += ['--test-filter', flt]

        for flt in self._additional_filters:
            cmd += ['--test-filter', flt]

        return cmd

    def get_type(self):
        return KTLINT_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.STYLE

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
                if devtools.ya.test.const.TEST_SUBTEST_SEPARATOR in x:
                    testname, subtest = x.split(devtools.ya.test.const.TEST_SUBTEST_SEPARATOR, 1)
                    result.append(test_common.SubtestInfo(testname, subtest))
            return result
        raise Exception(list_cmd_result.std_err)
