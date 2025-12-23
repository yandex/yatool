import os

import devtools.ya.test.const
import devtools.ya.test.util.tools
from devtools.ya.test import common as test_common
from devtools.ya.test.test_types import common as common_types


CLANG_TIDY_TEST_TYPE = "clang_tidy"


class ClangTidySuite(common_types.SemanticLinterSuite):
    def __init__(self, *args, **kwargs):
        super(ClangTidySuite, self).__init__(*args, **kwargs)
        self.clang_tidy_inputs = []
        self.library_path = None
        self.global_library_path = None

    @property
    def semantic_linter_inputs(self):
        """
        Property that returns tuple of semantic linter input files for ClangTidySuite.
        """
        return tuple(self.clang_tidy_inputs)

    def support_splitting(self, opts=None):
        """
        Does test suite support splitting
        """
        return False

    def support_recipes(cls):
        return False

    @property
    def requires_test_data(self):
        # clang-tidy test node doesn't require any test environment.
        # All work is done within compile nodes, while test node only process results.
        # Disabling test data allows to avoid extracting heavy test's dependencies like DATA(sbr:X), etc
        return False

    def get_test_related_paths(self, arc_root, opts):
        return []

    def fill_test_build_deps(self, graph):
        lib_path = os.path.join("$(BUILD_ROOT)", self.binary_path(''))
        if graph.get_uids_by_outputs(lib_path):
            self.library_path = lib_path
            self.clang_tidy_inputs.append(lib_path)
        glob_lib_path = self.global_tidy_library()
        if graph.get_uids_by_outputs(glob_lib_path):
            self.global_library_path = glob_lib_path
            self.clang_tidy_inputs.append(glob_lib_path)
        return super(ClangTidySuite, self).fill_test_build_deps(graph)

    def get_test_dependencies(self):
        # We need only LIBRARY\PROGRAM artifacts in deps
        deps = []
        if self.library_path:
            deps.append(self.library_path)
        if self.global_library_path:
            deps.append(self.global_library_path)
        return deps

    @property
    def supports_test_parameters(self):
        return False

    def support_list_node(self):
        """
        Does test suite support list_node before test run
        """
        return False

    def support_retries(self):
        return False

    @classmethod
    def get_ci_type_name(self):
        return "style"

    @property
    def cache_test_results(self):
        # suite is considered to be steady and cached by default

        # XXX Currently disabled due possible issues
        # For more info see DEVTOOLSSUPPORT-23013
        return False
        # return True

    def binary_path(self, root):
        return os.path.splitext(self.meta.binary_path)[0] + ".tidyjson"

    def get_run_cmd_inputs(self, opts):
        return self.clang_tidy_inputs

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
        cmd = devtools.ya.test.util.tools.get_test_tool_cmd(
            opts, 'run_clang_tidy', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            '--tracefile',
            os.path.join(test_work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            '--output-dir',
            os.path.join(test_work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
            '--project-path',
            self.project_path,
        ]
        for test_node_input in self.clang_tidy_inputs:
            cmd += ["--archive", test_node_input]
        for f in opts.tests_filters + self._additional_filters:
            cmd += ["--tests-filters", f]
        return cmd

    def get_type(self):
        return CLANG_TIDY_TEST_TYPE

    @property
    def class_type(self):
        return devtools.ya.test.const.SuiteClassType.STYLE

    def global_tidy_library(self):
        library_path = self.meta.global_library_path
        return library_path.replace("$B", "$(BUILD_ROOT)") if library_path.endswith(".tidyjson") else ""
