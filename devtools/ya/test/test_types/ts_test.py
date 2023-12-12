import logging
import os
import math

from test import const as test_const, common as test_common
from test.util import tools as test_tools
from test.test_types import common as common_types
from jbuild.gen import consts


logger = logging.getLogger(__name__)


JEST_TEST_TYPE = "jest"
HERMIONE_TEST_TYPE = "hermione"


class BaseTestSuite(common_types.AbstractTestSuite):
    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]

    @property
    def supports_clean_environment(self):
        return False

    def support_retries(self):
        return True

    @property
    def test_for_path(self):
        return os.path.join(consts.BUILD_ROOT, self.dart_info.get("TS-TEST-FOR-PATH"))

    def _get_run_cmd_opts(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            consts.BUILD_ROOT,
            self.project_path,
            self.name,
            retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            split_file=self._split_file_name,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )

        test_data_dirs = self.dart_info.get("TS-TEST-DATA-DIRS")
        test_data_dirs_rename = self.dart_info.get("TS-TEST-DATA-DIRS-RENAME")
        node_path = os.path.join(self.test_for_path, "node_modules")

        opts = [
            "--source-root",
            consts.SOURCE_ROOT,
            "--build-root",
            consts.BUILD_ROOT,
            "--project-path",
            self.project_path,
            "--test-work-dir",
            test_work_dir,
            "--output-dir",
            os.path.join(test_work_dir, test_const.TESTING_OUT_DIR_NAME),
            "--tracefile",
            os.path.join(test_work_dir, test_const.TRACE_FILE_NAME),
            "--test-for-path",
            self.test_for_path,
            "--node-path",
            node_path,
            "--nodejs",
            self.dart_info.get(self.dart_info.get("NODEJS-ROOT-VAR-NAME")),
        ]

        if test_data_dirs:
            opts += ["--test-data-dirs"] + test_data_dirs
            opts += ["--test-data-dirs-rename", test_data_dirs_rename]

        return opts


class JestTestSuite(BaseTestSuite):
    @classmethod
    def get_type_name(cls):
        return JEST_TEST_TYPE

    def get_type(self):
        return JEST_TEST_TYPE

    def support_splitting(self, opts=None):
        # TODO: Implement (https://st.yandex-team.ru/FEI-25459)
        return False

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        common_cmd_opts = self._get_run_cmd_opts(opts, retry, for_dist_build)
        generic_cmd = test_tools.get_test_tool_cmd(
            opts,
            "run_jest",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )

        cmd = (
            generic_cmd
            + common_cmd_opts
            + [
                "--config",
                self.dart_info.get("CONFIG-PATH"),
                "--timeout",
                str(self.timeout),
                "--verbose",
            ]
        )

        return cmd

    @property
    def supports_coverage(self):
        return True


class HermioneTestSuite(BaseTestSuite):
    @classmethod
    def get_type_name(cls):
        return HERMIONE_TEST_TYPE

    def get_type(self):
        return HERMIONE_TEST_TYPE

    def support_splitting(self, opts=None):
        return True

    @property
    def supports_coverage(self):
        return True

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        common_cmd_opts = self._get_run_cmd_opts(opts, retry, for_dist_build)
        test_files = sorted(self.dart_info.get("TEST-FILES")) or []
        generic_cmd = test_tools.get_test_tool_cmd(
            opts,
            "run_hermione",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )

        cmd = (
            generic_cmd
            + common_cmd_opts
            + [
                "--config",
                self.dart_info.get("CONFIG-PATH"),
                "--hermione",
                self.dart_info.get(self.dart_info.get("HERMIONE-ROOT-VAR-NAME") or "HERMIONE_RESOURCE_GLOBAL"),
            ]
        )

        if getattr(opts, "tests_filters", None):
            for flt in opts.tests_filters:
                cmd += ['--test-filter', flt]

        files_filter = getattr(opts, "test_files_filter", None)
        if files_filter:
            specified_test_paths = [os.path.normpath(f) for f in files_filter]
            cmd += specified_test_paths
        else:
            test_for_path = self.dart_info.get("TS-TEST-FOR-PATH")
            cmd += [os.path.relpath(f, test_for_path) for f in test_files]

        if self._modulo > 1:
            cmd += ["--chunks-count", str(self._modulo), "--run-chunk", str(self._modulo_index + 1)]

        return cmd


class EslintTestSuite(common_types.AbstractTestSuite):
    @classmethod
    def get_type_name(cls):
        return 'eslint'

    def get_type(self):
        return 'eslint'

    @classmethod
    def get_ci_type_name(cls):
        return 'style'

    def support_retries(self):
        return False

    def get_list_cmd(self, arc_root, build_root, opts):
        return []

    def __init__(
        self, dart_info, modulo=1, modulo_index=0, target_platform_descriptor=None, multi_target_platform_run=False
    ):
        super(EslintTestSuite, self).__init__(
            dart_info,
            modulo,
            modulo_index,
            target_platform_descriptor,
            split_file_name=None,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._eslint_config_path = self.dart_info.get('ESLINT_CONFIG_PATH')
        logger.debug(
            "SOURCE-FOLDER-PATH: '{}' ESLINT_CONFIG_PATH: '{}'".format(
                self.dart_info.get('SOURCE-FOLDER-PATH'), self._eslint_config_path
            )
        )

        self._nodejs_resource = self.dart_info.get(self.dart_info.get("NODEJS-ROOT-VAR-NAME"))
        self._eslint_resource = self.dart_info.get(
            self.dart_info.get("ESLINT-ROOT-VAR-NAME") or "ESLINT_RESOURCE_GLOBAL"
        )
        logger.debug("NODEJS_RESOURCE: '{}' ESLINT_RESOURCE: '{}'".format(self._nodejs_resource, self._eslint_resource))

        self._files = sorted(self.dart_info.get('TEST-FILES', []))
        self._file_processing_time = float(dart_info.get("LINT-FILE-PROCESSING-TIME") or "0.0")

    def support_splitting(self, opts=None):
        return self._file_processing_time > 0

    # we are splitting by `LINT-FILE-PROCESSING-TIME`
    # it allows us to fine tune a chunk size from build plugin (nots.py)
    # and it's auto adjusted to timeout setting
    def get_split_factor(self, opts):
        if opts and opts.testing_split_factor:
            return opts.testing_split_factor

        if self._files and self.support_splitting():
            return int(math.ceil(self._file_processing_time * len(self._files) / self.timeout))
        return 1

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default
        return True

    @property
    def supports_canonization(self):
        return False

    @property
    def supports_clean_environment(self):
        return False

    @property
    def supports_test_parameters(self):
        return True

    def binary_path(self, root):
        return os.path.join(self._source_folder_path(root), self.dart_info['TESTED-PROJECT-NAME'])

    def _tested_file_rel_path(self):
        return self.binary_path('')

    def get_test_related_paths(self, arc_root, opts):
        return [self._source_folder_path(arc_root)]

    def get_computed_test_names(self, opts):
        return ["{}::eslint".format(os.path.basename(filename)) for filename in self._files]

    def has_prepare_test_cmds(self):
        return True

    def get_prepare_test_cmds(self):
        inputs = [
            os.path.join(self._source_folder_path(consts.SOURCE_ROOT), self._eslint_config_path),
        ]
        return [], inputs

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        # test_work_dir: $(BUILD_ROOT)/devtools/dummy_arcadia/typescript/with_lint/test-results/eslint
        test_work_dir = test_common.get_test_suite_work_dir(
            consts.BUILD_ROOT,
            self.project_path,
            self.name,
            retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        cmd = test_tools.get_test_tool_cmd(
            opts, "run_eslint", self.global_resources, wrapper=True, run_on_target_platform=True
        )
        cmd += [
            "--source-root",
            consts.SOURCE_ROOT,
            "--build-root",
            consts.BUILD_ROOT,
            "--source-folder-path",
            self.dart_info.get('SOURCE-FOLDER-PATH'),
            "--nodejs",
            self._nodejs_resource,
            "--eslint",
            self._eslint_resource,
            "--eslint-config-path",
            self._eslint_config_path,
            "--tracefile",
            os.path.join(test_work_dir, test_const.TRACE_FILE_NAME),
        ]
        cmd += self._files[self._modulo_index :: self._modulo]
        return cmd
