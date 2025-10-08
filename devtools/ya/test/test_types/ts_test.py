import math
import os

import yalibrary.graph.const

import devtools.ya.test.common as test_common
import devtools.ya.test.const as test_const
import devtools.ya.test.test_types.common as common_types
import devtools.ya.test.util.tools as test_tools


JEST_TEST_TYPE = "jest"
HERMIONE_TEST_TYPE = "hermione"
PLAYWRIGHT_TEST_TYPE = "playwright"
PLAYWRIGHT_LARGE_TEST_TYPE = "playwright_large"

TS_MODULE_TAGS = ("ts", "ts_proto", "ts_proto_from_schema", "ts_proto_auto")
TS_TRANSIENT_MODULE_TAGS = TS_MODULE_TAGS + ("ts_prepare_deps",)


def get_nodejs_res(meta):
    if not meta.nodejs_root_var_name:
        raise Exception("Suite cannot find nodejs_root_var_name in metadata. Check configuration errors.")

    if not meta.nodejs_resource:
        raise Exception(
            "NodeJs resource is configured to be {}, but is not provided. Check configuration errors.".format(
                meta.nodejs_root_var_name
            )
        )

    return meta.nodejs_resource


class BaseFrontendSuite(common_types.AbstractTestSuite):
    @property
    def supports_clean_environment(self):
        return False

    @property
    def target_path(self):
        return self.meta.ts_test_for_path if self.meta and self.meta.ts_test_for_path else self.project_path

    @property
    def test_for_path(self):
        return os.path.join(yalibrary.graph.const.BUILD_ROOT, self.target_path)

    def setup_dependencies(self, graph):
        super(BaseFrontendSuite, self).setup_dependencies(graph)
        seen = set()

        for uid in self.get_build_dep_uids():
            self._propagate_ts_transient_deps(graph, uid, seen)

    def _propagate_ts_transient_deps(self, graph, uid, seen):
        """
        Ymake propagates deps for TS modules because they are configured as
            .PEERDIR_POLICY=as_build_from
            .NODE_TYPE=Bundle
        We need same logic for test node too.

        If test node has TS node in deps, then it also should include TS deps of that TS node.
        We need to add deps from one level only, because ymake set all
        """
        # Graph is a BuildPlan instance from devtools/ya/build/build_plan/build_plan.pyx
        node = graph.get_node_by_uid(uid)
        module_tag = graph.get_module_tag(node)

        if module_tag not in TS_MODULE_TAGS:
            # Ignore non-TS modules
            return

        for dep_uid in node.get("deps", []):
            if dep_uid in self._build_deps or dep_uid in seen:
                # Do not process same dep several times
                continue

            seen.add(dep_uid)
            project = graph.get_projects_by_uids(dep_uid)
            if not project:
                # Ignore non-module deps, only modules should be propagated
                continue
            project_path, toolchain, _, dep_module_tag, tags = project
            if dep_module_tag in TS_TRANSIENT_MODULE_TAGS:
                # Only add TS modules
                self.add_build_dep(project_path, toolchain, dep_uid, tags)


class BaseFrontendRegularSuite(BaseFrontendSuite):
    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]

    def support_retries(self):
        return True

    def _get_run_cmd_opts(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            yalibrary.graph.const.BUILD_ROOT,
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

        test_data_dirs = self.meta.ts_test_data_dirs
        test_data_dirs_rename = self.meta.ts_test_data_dirs_rename
        node_path = os.path.join(self.test_for_path, "node_modules")

        opts = [
            "--source-root",
            yalibrary.graph.const.SOURCE_ROOT,
            "--build-root",
            yalibrary.graph.const.BUILD_ROOT,
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
            get_nodejs_res(self.meta),
        ]

        if test_data_dirs:
            opts += ["--test-data-dirs"] + test_data_dirs
        if test_data_dirs_rename:
            opts += ["--test-data-dirs-rename", test_data_dirs_rename]

        return opts

    def get_test_dependencies(self):
        base_deps = super().get_test_dependencies()
        return sorted(set([os.path.join(self.target_path, "pre.pnpm-lock.yaml")] + base_deps))

    def get_run_cmd_inputs(self, opts):
        base_deps = super().get_run_cmd_inputs(opts)
        return sorted(set([os.path.join(self.target_path, "pre.pnpm-lock.yaml")] + base_deps))


class JestTestSuite(BaseFrontendRegularSuite):
    def get_type(self):
        return JEST_TEST_TYPE

    @property
    def class_type(self):
        return test_const.SuiteClassType.REGULAR

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
                self.meta.config_path,
                "--ts-config-path",
                self.meta.ts_config_path,
                "--timeout",
                str(self.timeout),
                "--verbose",
            ]
        )

        return cmd

    @property
    def supports_coverage(self):
        return True


class HermioneTestSuite(BaseFrontendRegularSuite):
    def get_type(self):
        return HERMIONE_TEST_TYPE

    @property
    def class_type(self):
        return test_const.SuiteClassType.REGULAR

    def support_splitting(self, opts=None):
        return True

    @property
    def supports_coverage(self):
        return True

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        common_cmd_opts = self._get_run_cmd_opts(opts, retry, for_dist_build)
        test_files = sorted(self.meta.test_files)
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
                self.meta.config_path,
            ]
        )

        if getattr(opts, "tests_filters", None):
            for flt in opts.tests_filters:
                cmd += ["--test-filter", flt]

        files_filter = getattr(opts, "test_files_filter", None)
        if files_filter:
            specified_test_paths = [os.path.normpath(f) for f in files_filter]
            cmd += specified_test_paths
        else:
            test_for_path = self.meta.ts_test_for_path
            cmd += [os.path.relpath(f, test_for_path) for f in test_files]

        if self._modulo > 1:
            cmd += [
                "--chunks-count",
                str(self._modulo),
                "--run-chunk",
                str(self._modulo_index + 1),
            ]

        return cmd


class PlaywrightLargeTestSuite(BaseFrontendRegularSuite):
    def get_type(self):
        return PLAYWRIGHT_LARGE_TEST_TYPE

    @property
    def class_type(self):
        return test_const.SuiteClassType.REGULAR

    def support_splitting(self, opts=None):
        return True

    @property
    def supports_coverage(self):
        return True

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        common_cmd_opts = self._get_run_cmd_opts(opts, retry, for_dist_build)
        test_files = sorted(self.meta.test_files)
        generic_cmd = test_tools.get_test_tool_cmd(
            opts,
            "run_playwright_large",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )

        cmd = (
            generic_cmd
            + common_cmd_opts
            + [
                "--config",
                self.meta.config_path,
            ]
        )

        if getattr(opts, "tests_filters", None):
            for flt in opts.tests_filters:
                cmd += ["--test-filter", flt]

        files_filter = getattr(opts, "test_files_filter", None)
        if files_filter:
            specified_test_paths = [os.path.normpath(f) for f in files_filter]
            cmd += specified_test_paths
        else:
            test_for_path = self.meta.ts_test_for_path
            cmd += [os.path.relpath(f, test_for_path) for f in test_files]

        if self._modulo > 1:
            cmd += [
                "--chunks-count",
                str(self._modulo),
                "--run-chunk",
                str(self._modulo_index + 1),
            ]

        return cmd


class PlaywrightTestSuite(BaseFrontendRegularSuite):
    def get_type(self):
        return PLAYWRIGHT_TEST_TYPE

    @property
    def class_type(self):
        return test_const.SuiteClassType.REGULAR

    def support_splitting(self, opts=None):
        return False

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        common_cmd_opts = self._get_run_cmd_opts(opts, retry, for_dist_build)
        generic_cmd = test_tools.get_test_tool_cmd(
            opts,
            "run_playwright",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )

        cmd = (
            generic_cmd
            + common_cmd_opts
            + [
                "--config",
                self.meta.config_path,
                "--ts-config-path",
                self.meta.ts_config_path,
            ]
        )

        return cmd

    @property
    def supports_coverage(self):
        return False


class AbstractFrontendStyleSuite(BaseFrontendSuite):
    @classmethod
    def get_ci_type_name(cls):
        return "style"

    def _get_config_files(self):
        """Should be implemented to handle changes in config files"""
        raise NotImplementedError

    def support_retries(self):
        return False

    def support_splitting(self, opts=None):
        return False

    @property
    def cache_test_results(self):
        return True

    @property
    def supports_canonization(self):
        return False

    def _abs_source_path(self, path):
        prefix = os.path.join(yalibrary.graph.const.SOURCE_ROOT, self.project_path)
        return path if path.startswith(prefix) else os.path.normpath(os.path.join(prefix, path))

    def _abs_build_path(self, path):
        prefix = os.path.join(yalibrary.graph.const.BUILD_ROOT, self.project_path)
        return path if path.startswith(prefix) else os.path.normpath(os.path.join(prefix, path))

    def _rel_from_abs_source(self, path):
        prefix = os.path.join(yalibrary.graph.const.SOURCE_ROOT, self.project_path)
        return os.path.relpath(path, prefix) if path.startswith(prefix) else path

    def get_test_dependencies(self):
        return sorted(
            set(
                [x for x in self.meta.custom_dependencies.split(" ") if x]
                + [self._abs_build_path("pre.pnpm-lock.yaml")]
            )
        )

    def get_run_cmd_inputs(self, opts):
        source_inputs = self._get_config_files() + ["package.json", "pnpm-lock.yaml"]
        return sorted(
            set(
                [self._abs_source_path(f) for f in source_inputs + self._files]
                + [self._abs_build_path("pre.pnpm-lock.yaml")]
            )
        )

    def get_test_related_paths(self, arc_root, opts):
        inputs = self.get_run_cmd_inputs(opts)

        return [
            f.replace(yalibrary.graph.const.SOURCE_ROOT, arc_root)
            for f in inputs
            if f.startswith(yalibrary.graph.const.SOURCE_ROOT)
        ]

    @property
    def test_run_cwd(self):
        return self._abs_build_path("")


class EslintTestSuite(AbstractFrontendStyleSuite):
    def __init__(
        self,
        meta,
        modulo=1,
        modulo_index=0,
        target_platform_descriptor=None,
        multi_target_platform_run=False,
    ):
        super(EslintTestSuite, self).__init__(
            meta,
            modulo,
            modulo_index,
            target_platform_descriptor,
            split_file_name=None,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._eslint_config_path = self.meta.eslint_config_path
        self._files = sorted(self.meta.test_files)
        self._file_processing_time = float(self.meta.lint_file_processing_time or "0.0")

    def get_type(self):
        return "eslint"

    @property
    def class_type(self):
        return test_const.SuiteClassType.STYLE

    def _get_config_files(self):
        return [self._eslint_config_path]

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

    def get_list_cmd(self, arc_root, build_root, opts):
        return []

    def get_computed_test_names(self, opts):
        return ["{}::eslint".format(os.path.basename(filename)) for filename in self._files]

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            yalibrary.graph.const.BUILD_ROOT,
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
            opts,
            "run_eslint",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )
        cmd += [
            "--source-root",
            yalibrary.graph.const.SOURCE_ROOT,
            "--build-root",
            yalibrary.graph.const.BUILD_ROOT,
            "--source-folder-path",
            self.meta.source_folder_path,
            "--nodejs",
            get_nodejs_res(self.meta),
            "--eslint-config-path",
            self._eslint_config_path,
            "--ts-config-path",
            self.meta.ts_config_path,
            "--tracefile",
            os.path.join(test_work_dir, test_const.TRACE_FILE_NAME),
        ]
        cmd += [self._rel_from_abs_source(f) for f in self._files[self._modulo_index :: self._modulo]]
        return cmd


class TscTypecheckTestSuite(AbstractFrontendStyleSuite):
    TS_FILES_EXTS = (".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs")

    def __init__(
        self,
        meta,
        modulo=1,
        modulo_index=0,
        target_platform_descriptor=None,
        multi_target_platform_run=False,
    ):
        super(TscTypecheckTestSuite, self).__init__(
            meta,
            modulo,
            modulo_index,
            target_platform_descriptor,
            split_file_name=None,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._ts_config_path = self.meta.ts_config_path
        self._files = [f.replace("$S/", yalibrary.graph.const.SOURCE_ROOT + "/") for f in sorted(self.meta.test_files)]

    def get_type(self):
        return "tsc_typecheck"

    @property
    def class_type(self):
        return test_const.SuiteClassType.STYLE

    def _get_config_files(self):
        return [self._ts_config_path]

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        # test_work_dir: $(BUILD_ROOT)/devtools/dummy_arcadia/typescript/with_lint/test-results/eslint
        test_work_dir = test_common.get_test_suite_work_dir(
            yalibrary.graph.const.BUILD_ROOT,
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
            opts,
            "run_tsc_typecheck",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )
        cmd += [
            "--source-root",
            yalibrary.graph.const.SOURCE_ROOT,
            "--build-root",
            yalibrary.graph.const.BUILD_ROOT,
            "--source-folder-path",
            self.meta.source_folder_path,
            "--nodejs",
            get_nodejs_res(self.meta),
            "--ts-config-path",
            self._ts_config_path,
            "--tracefile",
            os.path.join(test_work_dir, test_const.TRACE_FILE_NAME),
            "--log-path",
            os.path.join(test_work_dir, test_const.TESTING_OUT_DIR_NAME, "run_tsc_typecheck.log"),
        ]
        ts_files = [f for f in self._files if os.path.splitext(f)[1] in TscTypecheckTestSuite.TS_FILES_EXTS]
        cmd += ts_files[self._modulo_index :: self._modulo]
        return cmd


class StylelintTestSuite(AbstractFrontendStyleSuite):
    def __init__(
        self,
        meta,
        modulo=1,
        modulo_index=0,
        target_platform_descriptor=None,
        multi_target_platform_run=False,
    ):
        super(StylelintTestSuite, self).__init__(
            meta,
            modulo,
            modulo_index,
            target_platform_descriptor,
            split_file_name=None,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._files = self.meta.test_files

    def get_type(self):
        return "ts_stylelint"

    @property
    def class_type(self):
        return test_const.SuiteClassType.STYLE

    def _get_config_files(self):
        return [self.meta.ts_stylelint_config]

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir(
            yalibrary.graph.const.BUILD_ROOT,
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
            opts,
            "run_stylelint",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )

        cmd_args = [
            "--source-root",
            yalibrary.graph.const.SOURCE_ROOT,
            "--build-root",
            yalibrary.graph.const.BUILD_ROOT,
            "--project-path",
            self.meta.source_folder_path,
            "--nodejs-dir",
            get_nodejs_res(self.meta),
            "--test-config",
            self.meta.ts_stylelint_config,
            "--trace",
            os.path.join(test_work_dir, test_const.TRACE_FILE_NAME),
        ]

        return cmd + cmd_args + self._files[self._modulo_index :: self._modulo]

    def get_list_cmd(self, arc_root, build_root, opts):
        return self._files

    @classmethod
    def list(cls, cmd, cwd):
        return [test_common.SubtestInfo(f, "ts_stylelint") for f in cmd]
