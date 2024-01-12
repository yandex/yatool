# coding: utf-8

import os

import exts.windows

import test.const
from test.util import tools, shared
from test import common as test_common
from test.system import process
from test.test_types import common as common_types


FUZZ_TEST_TYPE = "fuzz"


class FuzzTestSuite(common_types.AbstractTestSuite):
    def __init__(self, *args, **kwargs):
        super(FuzzTestSuite, self).__init__(*args, **kwargs)
        self.add_python_before_cmd = False
        self.corpus_parts_limit_exceeded = 0

    def support_splitting(self, opts):
        """
        Does test suite support splitting
        """
        return not opts.fuzzing

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
            opts, 'run_fuzz', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            '--binary',
            self.binary_path('$(BUILD_ROOT)'),
            '--tracefile',
            os.path.join(test_work_dir, test.const.TRACE_FILE_NAME),
            '--modulo',
            str(self._modulo),
            '--modulo-index',
            str(self._modulo_index),
            '--output-dir',
            os.path.join(test_work_dir, test.const.TESTING_OUT_DIR_NAME),
            '--project-path',
            self.project_path,
            '--source-root',
            '$(SOURCE_ROOT)',
            '--verbose',
        ]

        if not exts.windows.on_win():
            cmd += ["--gdb-path", os.path.join("$(GDB)", "gdb", "bin", "gdb")]

        if opts and hasattr(opts, "tests_filters") and opts.tests_filters:
            for flt in opts.tests_filters:
                cmd += ['--test-filter', flt]

        for flt in self._additional_filters:
            cmd += ['--test-filter', flt]

        if for_dist_build and not getattr(opts, 'keep_full_test_logs', False):
            cmd.append("--truncate-logs")

        fuzz_opts = self.get_fuzz_opts(opts)
        if fuzz_opts:
            cmd.append("--fuzz-opts={}".format(fuzz_opts))

        fuzz_case_filename = getattr(opts, "fuzz_case_filename", None)
        if fuzz_case_filename:
            cmd += ["--fuzz-case", os.path.abspath(fuzz_case_filename)]

        if self.corpus_parts_limit_exceeded:
            cmd += ['--corpus-parts-limit-exceeded', str(self.corpus_parts_limit_exceeded)]

        if opts and getattr(opts, "fuzz_runs"):
            cmd += ["--fuzz-runs", str(opts.fuzz_runs)]

        if opts and getattr(opts, "fuzzing", False):
            cmd += ["--output-corpus-dir", os.path.join(test_work_dir, test.const.GENERATED_CORPUS_DIR_NAME)]
            cmd += ["--workers", str(self.requirements.get(test.const.TestRequirements.Cpu, 0))]
            for filename in self.get_fuzz_dicts():
                cmd += ["--fuzz-dict-path", os.path.join('$(SOURCE_ROOT)', filename)]

            if getattr(opts, "fuzz_minimization_only", False):
                cmd += ['--dummy-run']

            if getattr(opts, "fuzz_proof", 0):
                cmd += ['--fuzz-proof', str(opts.fuzz_proof)]

        return cmd

    @classmethod
    def get_type_name(cls):
        return FUZZ_TEST_TYPE

    def get_type(self):
        return FUZZ_TEST_TYPE

    @property
    def name(self):
        return FUZZ_TEST_TYPE

    def get_list_cmd(self, arc_root, build_root, opts):
        return self.get_run_cmd(opts) + ['--list']

    @classmethod
    def list(cls, cmd, cwd):
        return [
            test_common.SubtestInfo(*info)
            for info in shared.get_testcases_info(process.execute(cmd, check_exit_code=False, cwd=cwd))
        ]

    @property
    def supports_canonization(self):
        return False

    def support_retries(self):
        return True

    def get_computed_test_names(self, opts):
        return ["{}::test".format(self.get_type())]

    def get_test_related_paths(self, root, opts):
        paths = super(FuzzTestSuite, self).get_test_related_paths(root, opts)
        for filename in self.get_fuzz_dicts():
            paths.append(os.path.join(root, filename))
        return paths + [tools.get_corpus_data_path(self.project_path, root)]

    def get_fuzz_dicts(self):
        return self.dart_info.get('FUZZ-DICTS', [])

    def get_fuzz_opts(self, opts):
        parts = []
        if opts and getattr(opts, "fuzz_opts", False):
            parts.append(opts.fuzz_opts)
        for option in self.dart_info.get('FUZZ-OPTS', []):
            parts.append(option)
        return " ".join([_f for _f in parts if _f])

    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR2"]

    @property
    def supports_coverage(self):
        return True
