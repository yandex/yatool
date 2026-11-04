import os
import re

import yalibrary.graph.const

import devtools.ya.test.common as test_common
import devtools.ya.test.const as test_const
import devtools.ya.test.util.tools as test_tools

from . import common as common_types

TS_MODULE_TAGS = ("ts", "ts_proto", "ts_proto_from_schema", "ts_proto_auto")
TS_TRANSIENT_MODULE_TAGS = TS_MODULE_TAGS + ("ts_prepare_deps",)


class TS_CHECK_TYPES:
    LINT = "lint"
    TEST = "test"


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


class TsCheckSuite(BaseFrontendSuite):
    def __init__(
        self,
        meta,
        modulo=1,
        modulo_index=0,
        target_platform_descriptor=None,
        multi_target_platform_run=False,
    ):
        super(TsCheckSuite, self).__init__(
            meta,
            modulo,
            modulo_index,
            target_platform_descriptor,
            split_file_name=None,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._files = sorted(self.meta.test_files)
        self._check_type = self.meta.ts_check_type
        self._script_name = self.meta.test_name  # we use TEST_NAME to pass script name to runner
        self._command = self.meta.ts_check_command
        self._supports_coverage = self.meta.ts_check_has_coverage == "yes"
        # replace all none letters and none digits to '_'
        self._script_name_norm = re.sub(r"[\W_]+", "_", self._script_name)

    @property
    def class_type(self):
        return (
            test_const.SuiteClassType.STYLE
            if self._check_type == TS_CHECK_TYPES.LINT
            else test_const.SuiteClassType.REGULAR
        )

    def get_type(self):
        # IMPORTANT: test type cannot be `-` separated because of filtering logic
        # see: https://a.yandex-team.ru/arcadia/devtools/ya/test/filter/__init__.py?rev=r18884895#L105
        return "ts_{}_{}".format(self._check_type, self._script_name_norm)

    @property
    def name(self):
        return self.get_type()

    def get_ci_type_name(self):
        return "style" if self._check_type == TS_CHECK_TYPES.LINT else "test"

    def support_retries(self):
        return False

    def support_splitting(self, opts=None):
        # https://docs.yandex-team.ru/ya-make/manual/tests/common#parallel
        return False

    @property
    def supports_canonization(self):
        # https://docs.yandex-team.ru/ya-make/manual/tests/canon
        return False

    @property
    def supports_coverage(self):
        return self._supports_coverage

    @property
    def cache_test_results(self):
        # this is default for caching test results
        # if --cache-tests is passed (like in autocheck) this will be ignored
        # tests caching is based on testing node uid in graph
        # when cache_test_results=False node uid is random. when True - it is properly calculated from inputs

        # by convention style tests are cached by default
        return self.class_type == test_const.SuiteClassType.STYLE

    def _abs_source_path(self, path, arc_root=yalibrary.graph.const.SOURCE_ROOT):
        prefix = os.path.join(arc_root, self.target_path)
        return path if path.startswith(prefix) else os.path.normpath(os.path.join(prefix, path))

    def _abs_build_path(self, path):
        prefix = os.path.join(yalibrary.graph.const.BUILD_ROOT, self.target_path)
        return path if path.startswith(prefix) else os.path.normpath(os.path.join(prefix, path))

    def get_test_dependencies(self):
        # this list is used to build correct `deps` in test node
        # add package.json to dependencies so test node will be connected to PREPARE_DEPS
        # self.meta.custom_dependencies are from DEPENDS macro
        return sorted(
            set(
                [x for x in self.meta.custom_dependencies.split(" ") if x and not x == "$TEST_DEPENDS_VALUE"]
                + [self._abs_build_path("package.json")]
            )
        )

    def get_test_related_paths(self, arc_root, opts):
        # these files are used to actually calculate test node UID
        # see: https://a.yandex-team.ru/arcadia/devtools/ya/test/dependency/uid.py?rev=r18941132#L46

        extra_source_files = [
            # we need it to copy to BINDIR
            "package.json",
        ]
        all_files = extra_source_files + self._files
        return sorted(set([self._abs_source_path(f, arc_root) for f in all_files]))

    def get_run_cmd_inputs(self, opts):
        # this list does not affect graph at all
        # get_test_dependencies are used to fill `deps` correctly
        # get_test_related_paths are used to contribute in uid
        # moreover inputs list in graph is appended by get_test_related_paths
        # see: https://a.yandex-team.ru/arcadia/devtools/ya/test/test_node/__init__.py?rev=r18941230#L616
        return []

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
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
        outdir = os.path.join(test_work_dir, test_const.TESTING_OUT_DIR_NAME)
        generic_cmd = test_tools.get_test_tool_cmd(
            opts,
            "run_ts_check",
            self.global_resources,
            wrapper=True,
            run_on_target_platform=True,
        )

        cmd = generic_cmd + [
            "--source-root",
            yalibrary.graph.const.SOURCE_ROOT,
            "--build-root",
            yalibrary.graph.const.BUILD_ROOT,
            "--output-dir",
            outdir,
            "--tracefile",
            os.path.join(test_work_dir, test_const.TRACE_FILE_NAME),
            "--log-path",
            os.path.join(outdir, "ts-check.log"),
            "--target_path",
            self.target_path,
            "--nodejs",
            get_nodejs_res(self.meta),
            "--script-name",
            self._script_name,
        ]
        if self._command:
            cmd += ["--command", self._command]
        cmd += ["--test-type", self.get_type()]

        return cmd + self._files
