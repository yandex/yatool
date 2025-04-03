# coding: utf-8

import argparse
import contextlib
import datetime
import errno
import json
import locale
import logging
import math
import io
import os
import re
import shlex
import shutil
import signal
import six
import socket
import stat
import sys
import time
import traceback

from . import test_context
from .stages import Stages

import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage
from devtools.ya.test import dartfile
from devtools.ya.test import tracefile
from devtools.ya.test.programs.test_tool.lib import monitor
from devtools.ya.test.programs.test_tool.lib import runtime
from devtools.ya.test.programs.test_tool.lib import secret
from devtools.ya.test.programs.test_tool.lib import testroot
from devtools.ya.test.programs.test_tool.lib import tmpfs
from devtools.ya.test.programs.test_tool.lib import unshare
from devtools.ya.test.programs.test_tool.lib.report import chunk_result
from library.python.testing import system_info
from devtools.ya.test import common as test_common
from devtools.ya.test import const
from devtools.ya.test.system import process, env as system_env
from devtools.ya.test.util import shared
from devtools.ya.test.util import tools
from yalibrary import term, formatter
from yalibrary.display import strip_markup
from yatest.common import cores

from devtools.ya.test.dependency import mds_storage
from devtools.ya.test.dependency import sandbox_storage

import exts.archive
import exts.fs
import exts.uniq_id
import devtools.ya.test.common
import devtools.ya.test.reports
import devtools.ya.test.result
import devtools.ya.test.test_types.common
import devtools.ya.test.util.shared

logger = logging.getLogger(__name__)

RECIPE_ERROR_MESSAGE_LIMIT = 1024
SMOOTH_SHUTDOWN_TIMEOUT = 30
TESTS_FAILED_EXIT_CODE = 66
TIMEOUT_KILL_SIGNAL = 3  # signal.SIGQUIT
PSTREE_CMDLINE_LIMIT = 200


class TestError(Exception):
    pass


class TestRunTimeExhausted(TestError):
    pass


class RecipeError(TestError):
    def __init__(self, name, err_filename, out_filename):
        super(RecipeError, self).__init__()
        self.name = name
        self.err_filename = err_filename
        self.out_filename = out_filename
        self.err_snippet = read_tail(err_filename).strip()

    def format_full(self):
        return "{}\n[[bad]]Stderr tail:[[bad]]{}[[rst]]".format(
            self.format_info(), self.err_snippet[-RECIPE_ERROR_MESSAGE_LIMIT:]
        )

    def format_info(self):
        return "[[bad]]{}: {} failed".format(type(self).__name__, self.name)


class RecipeStartUpError(RecipeError):
    pass


class RecipeTearDownError(RecipeError):
    pass


def log_stage(name):
    logger.debug("%s", "{s:{c}^{n}}".format(s=" {} ".format(name.upper()), n=80, c="="))


stages = Stages("suite", stage_callback=log_stage)


def run_with_gdb(gdb_path, tty, exec_func, source_root):
    def wrapper(*args, **kwargs):
        command = [gdb_path]
        # TODO keep args in sync with run_under_gdb devtools/ya/test/util/shared.py untill YA-724 is done
        command += ["-iex", "set demangle-style none"]
        command += ["-ex", "set demangle-style auto"]
        command += ["-ex", "set substitute-path /-S/ {}/".format(source_root)]
        command += ["-ex", "set filename-display absolute"]
        command += ["--args"]
        kwargs["command"] = command + kwargs["command"]

        for name in ["stdout", "stderr"]:
            if name in kwargs:
                kwargs[name] = tty

        old_attrs = term.console.connect_real_tty(tty)
        try:
            return exec_func(*args, **kwargs)
        finally:
            term.console.restore_referral(*old_attrs)

    return wrapper


class RunStages(object):
    """
    Class to count and name run test stages
    """

    TST_START_RECP = 1
    TST_RUN_TEST = 2
    TST_STOP_RECP = 3

    def __init__(self, options):
        self.stage = 0
        self.out_dir = get_output_path(get_test_work_dir(options))

    def get_log_name(self, step, cmd):
        stage = self.stage
        self.stage += 1

        if stage == 0:
            return os.path.join(self.out_dir, "stage_0_start_sys_info.log")
        if step == RunStages.TST_START_RECP:
            return os.path.join(
                self.out_dir, "stage_{}_{}_{}_sys_info.log".format(stage, "recipe_start", os.path.basename(cmd[0]))
            )
        if step == RunStages.TST_RUN_TEST:
            return os.path.join(self.out_dir, "stage_{}_{}_sys_info.log".format(stage, "test_finished"))
        if step == RunStages.TST_STOP_RECP:
            return os.path.join(
                self.out_dir, "stage_{}_{}_{}_sys_info.log".format(stage, "recipe_stop", os.path.basename(cmd[0]))
            )
        return os.path.join(self.out_dir, "stage_{}_{}_sys_info.log".format(stage, "unknown"))


@contextlib.contextmanager
def reserve_disk_space(disk_size, stages):
    t = time.time()
    fname = "__reserved"
    with open(fname, 'w') as afile:
        afile.seek(disk_size)
        afile.write("!")

    stages.add("reserve_disk_space", time.time() - t)

    try:
        yield
    finally:
        os.remove(fname)


def sigalarm_handler(signum, frame):
    logger.debug(
        "[[imp]]TS: %d Current stage: %s Stages: %s[[rst]]", time.time(), stages.get_current_stage(), stages.dump()
    )


def postprocess_outputs(suite, tags, output_dir, truncate=False, truncate_limit=None, keep_symlinks=False):
    stages.stage("postprocess_outputs")

    if keep_symlinks:
        logger.debug("Symlinks will be kept")
    else:
        logger.debug("Removing symlinks: %s", output_dir)
        tools.remove_links(output_dir)

    if truncate and truncate_limit is not None:
        output_limit = truncate_limit
    else:
        output_limit = None

    logger.debug("Postprocessing %s (output_limit:%s)", output_dir, output_limit)
    file_size_map = {}
    for root, _, files in os.walk(output_dir):
        for filename in files:
            path = os.path.join(root, filename)
            # Remove non-regular and non-symlink files to avoid problems with
            # - archiving test outputs
            # - storing test outputs in the cache with dir-outputs mode enabled
            if not os.path.isfile(path):
                try:
                    os.remove(path)
                except Exception as e:
                    logger.error('Failed to remove non-regular file %s: %s', path, e)
                else:
                    logger.info('Removed non-regular file: %s ', path)
            elif os.path.basename(path) not in const.TRUNCATING_IGNORE_FILE_LIST:
                file_size_map[os.path.join(root, filename)] = os.stat(path).st_size

    total_output_size = sum(six.itervalues(file_size_map))

    metrics = suite.chunk.metrics
    metrics.update(
        {
            'output_dir_files': len(file_size_map),
            'output_dir_size_in_kb': total_output_size // 1024,
        }
    )

    def truncate(filename, orig_size, trunc_size):
        logger.debug(
            'Postprocess: %s is truncated up to %s bytes (original size is %s bytes)', filename, trunc_size, orig_size
        )
        try:
            os.chmod(filename, 0o666)
            # It's meaningless to truncate archives in the middle
            if exts.archive.is_archive_type(filename):
                tools.truncate_tail(filename, trunc_size)
            else:
                tools.truncate_middle(filename, trunc_size, msg="\n..[truncated]..\n")
        except Exception as e:
            logger.exception("Error while postprocessing file %s (%s). Trying to drop it.", filename, e)
            try:
                os.remove(filename)
            except Exception as e:
                logger.exception("Drop failure: %s", e)

    if output_limit is not None and total_output_size > output_limit:
        logger.debug(
            "Going to process %s files with total output size: %db (%s)",
            len(file_size_map),
            total_output_size,
            formatter.format_size(total_output_size),
        )

        if 'ya:full_logs' in tags:
            msg = "[[bad]]Output logs are truncated and it's treated as error due [[imp]]ya:full_logs[[bad]] tag. Limit: [[imp]]{lim}[[bad]] Output size: [[imp]]{size}[[bad]]".format(
                lim=output_limit,
                size=total_output_size,
            )
            suite.add_chunk_error(msg)
            logger.warning(strip_markup(msg))

        fifth = output_limit // 5
        skip_size, trunc_size = 0, 0
        files2trunc = {}
        for filename, size in sorted(six.iteritems(file_size_map), key=lambda x: x[1]):
            if skip_size + size > fifth:
                files2trunc[filename] = size
                trunc_size += size
            else:
                skip_size += size

        limit = output_limit - skip_size
        for filename, size in files2trunc.items():
            truncate(filename, size, int(float(size) / trunc_size * limit))

    logger.debug("Postprocessing finished")


def parse_args(args=None):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--test-suite-name", dest="test_suite_name", help="name of the running test suite", default=None
    )
    parser.add_argument("--test-list-path", dest="test_list_path", help="path to tests list", default=None)
    parser.add_argument("--project-path", dest="project_path", help="project path arcadia root related")
    parser.add_argument(
        "--autocheck-mode",
        dest="autocheck_mode",
        help="run tests with autocheck restrictions",
        default=False,
        action="store_true",
    )
    parser.add_argument("--target-platform-descriptor", dest="target_platform_descriptor")
    parser.add_argument(
        "--multi-target-platform-run", dest="multi_target_platform_run", action='store_true', default=False
    )
    parser.add_argument("--test-size", dest="test_size", help="test size (e.g. 'large')")
    parser.add_argument("--test-type", dest="test_type", help="suite type (e.g. 'pytest')")
    parser.add_argument("--test-ci-type", dest="test_ci_type", help="suite CI type (e.g. 'style')")
    parser.add_argument("--stdout", dest="stdout", help="file to save stdout")
    parser.add_argument("--stderr", dest="stderr", help="file to save stderr")
    parser.add_argument("--tar", dest="tar", help="file to save tar with testing_out_stuff")
    parser.add_argument("--compression-filter", help="Specifies compression filter to the output tar archive")
    parser.add_argument("--compression-level", type=int, help="Specifies compression level to the output tar archive")
    parser.add_argument(
        "--space-to-reserve",
        type=int,
        help="Space to reserve before running test wrapper and recipes",
        default=0,
        required=False,
    )
    parser.add_argument("--meta", dest="meta", help="file to save meta data")
    parser.add_argument("--trace", dest="trace", help="file to save trace")
    parser.add_argument("--trace-wreckage", dest="trace_wreckage", help="wreckage tracefile with extra errors")
    parser.add_argument("--recipes", dest="recipes", help="base64-encoded recipes", action='store', default="")
    parser.add_argument("--source-root", dest="source_root", help="source route", action='store')
    parser.add_argument("--build-root", dest="build_root", help="build route", action='store')
    parser.add_argument("--test-data-root", dest="data_root", help="test data route", action='store')
    parser.add_argument(
        "--sandbox-resources-root", dest="sandbox_resources_root", help="sandbox resources root", action='store'
    )
    parser.add_argument(
        "--test-related-path",
        dest="test_related_paths",
        help="list of paths requested by wrapper (suite) or test - these paths will form PYTHONPATH",
        action='append',
        default=None,
    )
    parser.add_argument(
        "--test-data-path",
        dest="test_data_paths",
        help="list of test data paths that test declared to be dependent on",
        action='append',
        default=None,
    )
    parser.add_argument(
        "--python-sys-path",
        dest="python_sys_paths",
        help="list of paths that test should add to sys.path",
        action='append',
        default=[],
    )
    parser.add_argument(
        "--sandbox-resource",
        dest="sandbox_resources",
        help="list of sandbox resources that test depends on",
        action="append",
        default=None,
    )
    parser.add_argument(
        "--external-local-file",
        dest="external_local_files",
        help="list of external local files that test depends on",
        action="append",
        default=None,
    )
    parser.add_argument("--node-timeout", dest="node_timeout", type=int, help="run_test timeout", action='store')
    parser.add_argument("--timeout", dest="timeout", type=int, help="test timeout", action='store')
    parser.add_argument("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument(
        "--truncate-files", dest="truncate_files", help="Truncate big files", action='store_true', default=False
    )
    parser.add_argument(
        "--truncate-files-limit",
        dest="truncate_files_limit",
        type=int,
        help="Sets output files limit (bytes)",
        action="store",
    )
    parser.add_argument(
        "--verify-results",
        dest="verify",
        help="perform result verification against the canonical one",
        action='store_true',
    )
    parser.add_argument(
        "--custom-canondata-path",
        dest="custom_canondata_path",
        help="Verify canondata against custom canondata",
        action='store',
        default=None,
    )
    parser.add_argument(
        "--dont-verify-results",
        dest="verify",
        help="do not perform result verification against the canonical one",
        action='store_false',
    )
    parser.add_argument(
        "--result-resource-owner", dest="result_resource_owner", help="uploaded resource owner", action='store'
    )
    parser.add_argument(
        "--result-resource-owner-key",
        dest="result_resource_owner_key",
        help="uploaded resource owner key",
        action='append',
        default=[],
    )
    parser.add_argument(
        "--result-resource-ttl",
        dest="result_resource_ttl",
        type=int,
        help="uploaded from test result resource TTL (days)",
        action='store',
        default=30,
    )
    parser.add_argument(
        "--result-max-file-size",
        dest="result_max_file_size",
        type=int,
        help="max file size to store locally (0 - no limit)",
        action='store',
        default=100 * 1024,
    )
    parser.add_argument(
        "--max-inline-diff-size",
        dest="max_inline_diff_size",
        type=int,
        help="max diff size to be inlined in the assertion message (0 - no limit)",
        action='store',
        default=1024,
    )
    parser.add_argument(
        "--max-test-comment-size",
        type=int,
        help="max diff size to be inlined in the assertion message (0 - no limit)",
        action='store',
        default=5000,
    )
    parser.add_argument("--retry", dest="retry", help="retry no", action='store', type=int, default=None)
    parser.add_argument("--test-stderr", dest="test_stderr", help="output stderr", action='store_true', default=False)
    parser.add_argument("--test-stdout", dest="test_stdout", help="output stdout", action='store_true', default=False)
    parser.add_argument("--test-run-cwd", dest="test_run_cwd", help="test run cwd", default=None)
    parser.add_argument(
        "--show-test-cwd", action="store_true", help="show test cwd in the console report", default=False
    )
    parser.add_argument("--tag", dest="test_tags", action="append", help="Suite tags", default=[])
    parser.add_argument("--env", dest="test_env", action="append", help="Test env", default=[])
    parser.add_argument("--no-clean-environment", dest="create_clean_environment", action='store_false', default=True)
    parser.add_argument("--supports-canonization", dest="supports_canonization", action='store_true', default=False)
    parser.add_argument("--supports-test-parameters", action='store_true')
    parser.add_argument("--keep-symlinks", action='store_true', default=False)
    parser.add_argument("--split-count", action='store', type=int, default=1)
    parser.add_argument("--split-index", action='store', type=int, default=0)
    parser.add_argument("--split-file", action='store', default=None)
    parser.add_argument("--should-tar-dir-outputs", help='create tared dir outputs', action='store_true', default=False)
    parser.add_argument("--cpp-coverage-path", dest="cpp_coverage_path", default=None)
    parser.add_argument("--java-coverage-path", dest="java_coverage_path", default=None)
    parser.add_argument("--sancov-coverage", dest="sancov_coverage", action='store_true', default=None)
    parser.add_argument("--clang-coverage", dest="clang_coverage", action='store_true', default=None)
    parser.add_argument("--go-coverage-path", dest="go_coverage_path", default=None)
    parser.add_argument("--fast-clang-coverage-merge", dest="fast_clang_coverage_merge", action="store")
    parser.add_argument("--python3-coverage-path", dest="python3_coverage_path", default=None)
    parser.add_argument("--ts-coverage-path", dest="ts_coverage_path", default=None)
    parser.add_argument("--nlg-coverage-path", dest="nlg_coverage_path", default=None)
    parser.add_argument("--coverage-prefix-filter", dest="coverage_prefix_filter", default="")
    parser.add_argument("--coverage-exclude-regexp", dest="coverage_exclude_regexp", default="")
    parser.add_argument("--dont-replace-roots", dest="replace_roots", action='store_false', default=True)
    parser.add_argument("--show-test-pid", action='store_true', default=False)
    parser.add_argument("--same-process-group", help="don't setpgrp", action="store_true", default=False)
    parser.add_argument("--fuzz-corpus-tar", dest="fuzz_corpus_tar")
    parser.add_argument("--smooth-shutdown-signals", dest="smooth_shutdown_signals", action="append", default=[])
    parser.add_argument("--with-wine", dest="with_wine", action="store_true", default=False)
    parser.add_argument("--requires-ram-disk", dest="requires_ram_disk", action="store_true", default=False)
    parser.add_argument("--ram-limit-gb", type=int, default=0)
    parser.add_argument("--global-resource", dest="global_resources", action="append", default=[])
    parser.add_argument("--setup-pythonpath-env", action='store_true', default=False)
    parser.add_argument("--pdb", action="store_true", default=False)
    parser.add_argument("--gdb-debug", action="store_true", help="Test under gdb")
    parser.add_argument("--gdb-path", default="gdb", help="Path to gdb")
    parser.add_argument("--python-bin", help="Path to python")
    parser.add_argument("--python-lib-path", help="Path to python lib")
    parser.add_argument("--keep-temps", action="store_true", default=False)
    parser.add_argument("--dir-outputs", action="store_true", default=False)
    parser.add_argument("--remove-tos", action="store_true", default=False)
    parser.add_argument(
        "--allure", dest="allure", help="specifies path to the output tar with allure report", default=None
    )
    parser.add_argument(
        "--local-ram-drive-size", default=0, type=int, help="Use local ram drive for tests with specified size in GiB"
    )
    parser.add_argument("--output-style", dest="output_style", help="output style", default=None)
    parser.add_argument("--sub-path", dest='sub_path', help="Canonical sub path", default=None)
    parser.add_argument(
        "--test-param", action="append", dest="test_params", default=[], help="test parameters specified by user"
    )
    parser.add_argument("--context-filename", help="Path to the common context", default=None)
    parser.add_argument(
        "--store-original-tracefile-tar", help="Path to file to store original tracefile from test wrapper"
    )
    parser.add_argument("--trace-output-filename")
    parser.add_argument("--propagate-timeout-info", action='store_true')
    parser.add_argument("--dump-test-environment", action='store_true')
    parser.add_argument("--dump-node-environment", action='store_true')
    parser.add_argument("--prepare-only", action="store_true", default=False)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    parser.add_argument(
        "--data-to-environment",
        dest="env_data_mode",
        help="mode of DATA() apply to environment",
        choices=[testroot.EnvDataMode.Symlinks, testroot.EnvDataMode.Copy, testroot.EnvDataMode.CopyReadOnly],
        default=testroot.EnvDataMode.Symlinks,
    )
    parser.add_argument(
        "--disable-memory-snippet",
        dest='disable_memory_snippet',
        help="Disable memory monitor snippet",
        action='store_true',
    )
    parser.add_argument("--tests-limit-in-chunk", action='store', type=int, default=0)
    parser.add_argument("--create-root-guidance-file", action='store_true')
    parser.add_argument("--pycache-prefix")

    args = parser.parse_args(args)

    if "--profile-wrapper" in args.command:
        args.command.remove("--profile-wrapper")
        args.command.append("--profile-test-tool")

    if args.trace and not args.trace_wreckage:
        args.trace_wreckage = args.trace + '.wreckage'

    args.global_resources = dict(x.split('::', 1) for x in args.global_resources)

    args.store_cores = True
    if args.truncate_files:
        args.store_cores = False

    if args.test_params:
        test_params = {}
        for test_param in args.test_params:
            if "=" in test_param:
                k, v = test_param.split("=", 1)
                test_params[k] = v
            else:
                test_params[test_param] = True
        args.test_params = test_params
    else:
        args.test_params = dict()

    if args.truncate_files_limit is None:
        # env.var is specified by distbuild's worker
        limit = os.environ.get('YA_TEST_NODE_OUTPUT_LIMIT_IN_BYTES')
        if limit is not None:
            args.truncate_files_limit = int(limit)

    return args


class StdErrWatcher(object):
    def open(self, command, process, out_file, err_file):
        self._err_read_file = open(err_file.name)

    def close(self):
        self._err_read_file.close()

    def __call__(self, *args, **kwargs):
        text = self._err_read_file.read()
        if text:
            for line in text.splitlines():
                sys.stderr.write("##{}\n".format(line))
            sys.stderr.flush()


class TraceFileWatcher(object):
    def __init__(self, trace_report, test_cwd, output_style, test_stderr):
        self._trace_report = trace_report
        self._test_cwd = test_cwd
        self.pid = None
        self._file = None
        self._buffer = []
        self._chunk_size = 8192
        self._output_style = output_style
        self._test_stderr = test_stderr

    def open(self, command, process, out_file, err_file):
        self._displayed_tests = set()

    def close(self):
        if self._file:
            self._proceed()
            self._file.close()
        self._buffer = []

    def __call__(self):
        self._proceed()

    def _proceed(self):
        # noinspection PyUnresolvedReferences
        import app_ctx

        trace_report = self._trace_report
        if not os.path.exists(trace_report):
            return

        if not self._file:
            self._file = io.open(trace_report, errors='ignore', encoding='utf-8')

        # try to accumulate some data before parsing
        # and avoid buffer oversaturation
        for _ in range(10):
            data = self._file.read(self._chunk_size)
            if data:
                self._buffer.append(data)
            if len(data) < self._chunk_size:
                break

        if not self._buffer:
            return

        # there are no compete line
        if not any('\n' in d for d in self._buffer):
            return

        data = ''.join(self._buffer)
        full, tail = data.rsplit('\n', 1)

        if tail:
            self._buffer = [tail]
        else:
            self._buffer = []

        if full:
            try:
                if self._test_stderr:
                    reporter = devtools.ya.test.reports.StdErrReporter()
                else:
                    reporter = None

                result = tracefile.TestTraceParser.parse_from_string(full, reporter=reporter, relaxed=True)
                for testcase in result.tests:
                    if (
                        testcase.status
                        not in [devtools.ya.test.common.Status.NOT_LAUNCHED, devtools.ya.test.common.Status.DESELECTED]
                        and (testcase, testcase.status) not in self._displayed_tests
                    ):
                        msg = self._get_testcase_msg(testcase)

                        if self.pid:
                            msg += " (pid: {})".format(self.pid)
                        if self._test_cwd:
                            msg += " in {}".format(self._test_cwd)
                        status_msg = msg + "\n"

                        if self._output_style == 'make':
                            app_ctx.display.emit_message("##" + msg + "\n")

                        app_ctx.display.emit_status(status_msg)

                        self._displayed_tests.add((testcase, testcase.status))
            except Exception:
                logger.warning("Error while processing chunk of the trace file: %s", traceback.format_exc())

    def _get_testcase_msg(self, testcase):
        status_as_str = devtools.ya.test.const.Status.TO_STR[testcase.status]
        status_color = devtools.ya.test.const.StatusColorMap[status_as_str]

        # We set CRASHED status and the comment by default in case segfault occurred
        # so if wee see it like that, then we're sure test is being running
        # that's why we display it without status
        if (
            testcase.status == devtools.ya.test.const.Status.CRASHED
            and testcase.comment == devtools.ya.test.const.DEFAULT_CRASHED_STATUS_COMMENT
        ):
            return ">> " + testcase.name
        return ">> " + testcase.name + " [[[" + status_color + "]]" + status_as_str.upper() + "[[rst]]]"


class CompositeProcessWatcher(object):
    def __init__(self, watchers):
        self._watchers = watchers

    def open(self, command, process, out_file, err_file):
        for watcher in self._watchers:
            watcher.open(command, process, out_file, err_file)

    def close(self):
        for watcher in self._watchers:
            watcher.close()

    def __call__(self, *args, **kwargs):
        for watcher in self._watchers:
            watcher(*args, **kwargs)


def need_to_rerun_test(stderr, return_code, try_num, tags, failed_recipes, test_size):
    if failed_recipes:
        return False
    if stderr is None:
        return True
    if tags and const.YaTestTags.Norestart in tags:
        return False
    if try_num >= const.MAX_TEST_RESTART_COUNT + 1:
        return False
    if return_code == 0:
        return False
    if const.INFRASTRUCTURE_ERROR_INDICATOR in stderr and test_size == const.TestSize.Large:
        sys.exit(const.TestRunExitCode.InfrastructureError)
    for indicator in const.RESTART_TEST_INDICATORS:
        if indicator in stderr:
            logger.info("Found restart test indicator '%s' in stderr", indicator)
            return True
    return False


def move_test_work_dir(src, dst, cmd):
    exts.fs.ensure_dir(dst)
    for f in os.listdir(src):
        if f != "run_test.log":
            exts.fs.move(os.path.join(src, f), os.path.join(dst, f))


def extend_recipe_cmd(args, options, test_cmd, action, env_file=None):
    recipe_args = [
        os.path.join(options.build_root, args[0]),
        "--build-root",
        options.build_root,
        "--source-root",
        options.source_root,
        "--gdb-path",
        options.gdb_path,
    ]
    if options.pdb:
        recipe_args += ["--pdb"]
    if options.show_test_cwd:
        recipe_args += ["--show-cwd"]
    if env_file:
        recipe_args += [
            "--env-file",
            env_file,
        ]
    recipe_args += test_cmd + [action] + args[1:]
    return recipe_args


def get_recipes_env(env_file):
    env = dict()
    if not os.path.exists(env_file):
        return env

    with open(env_file) as f:
        for line in f.readlines():
            data = json.loads(line)

            for k, v in data.items():
                env[test_common.to_utf8(k)] = test_common.to_utf8(v)
    return env


def replace_roots(suite, new_source_root, command_cwd, build_root):
    replacements = [
        (new_source_root, "$(SOURCE_ROOT)"),
        (build_root, "$(BUILD_ROOT)"),
    ]

    # XXX - this path should remain as is - will convert it back
    real_replacements = [(os.path.join(build_root, "canon_tmp"), os.path.join("$(BUILD_ROOT)", "canon_tmp"))]

    # try to replace real path first
    for path, replacement in replacements:
        if os.path.realpath(path) != path:
            real_replacements.append((os.path.realpath(path), replacement))
        real_replacements.append((path, replacement))

    # XXX - this path should remain as is - convert it back
    real_replacements.append((os.path.join("$(BUILD_ROOT)", "canon_tmp"), os.path.join(build_root, "canon_tmp")))

    try:
        # some runners, e.g. pytest report tracebacks with path relative to the cwd, which causes undesired snipptets
        # (../../../../../../../../environment/arcadia/...) - this replacement fixes that
        command_cwd_source_root_rel_path = os.path.join(
            os.path.relpath(build_root, command_cwd), "environment", "arcadia"
        )
        real_replacements.insert(0, (command_cwd_source_root_rel_path, "$(SOURCE_ROOT)"))
    except ValueError as e:
        logger.debug("Could not add command_cwd_source_root_rel_path replacement: %s", e)

    resolver = devtools.ya.test.reports.TextTransformer(real_replacements)
    logger.debug("Fixing roots using resolver %s", resolver)
    suite.fix_roots(resolver)


def fix_python_path(env, test_related_paths, source_root, new_source_root, build_root, with_wine=False):
    logger.debug("Changing python paths: %s", tools.get_python_paths(env))

    python_paths = devtools.ya.test.util.shared.change_cmd_root(
        test_related_paths, source_root, new_source_root, build_root
    )
    python_dirs = set()
    for p in python_paths:
        if os.path.isfile(p):
            python_dirs.add(os.path.dirname(p))
        else:
            python_dirs.add(p)
    tools.append_python_paths(env, python_dirs, overwrite=True)

    if with_wine:
        env["PYTHONPATH"] = env["PYTHONPATH"].replace(":", ";")

    return env


def generate_suite(options, work_dir):
    suite = devtools.ya.test.test_types.common.PerformedTestSuite(
        options.test_suite_name,
        options.project_path,
        size=options.test_size,
        tags=options.test_tags,
        target_platform_descriptor=options.target_platform_descriptor,
        multi_target_platform_run=options.multi_target_platform_run,
    )
    suite.set_work_dir(work_dir)
    register_chunk(suite, options)
    return suite


def register_chunk(suite, options):
    suite.register_chunk(
        nchunks=int(options.split_count) if options.split_count else 1,
        chunk_index=int(options.split_index),
        filename=options.split_file,
    )


def update_chunk_logs(suite, options):
    for ttype, filename in [
        ("stdout", suite.stdout_path()),
        ("stderr", suite.stderr_path()),
        ("messages", suite.messages_path()),
        ("logsdir", suite.output_dir()),
        ("log", options.log_path),
        ("trace_output", options.trace_output_filename),
    ]:
        if filename and os.path.exists(filename) and ttype not in suite.chunk.logs:
            suite.chunk.logs[ttype] = filename

    # trace output log will be created when the current process exits
    if options.trace_output_filename:
        suite.chunk.logs['trace_output'] = options.trace_output_filename

    for ttype, filename in [("logsdir", suite.output_dir())]:
        for tst in suite.chunk.tests:
            if ttype not in tst.logs and os.path.exists(filename):
                tst.logs[ttype] = filename


def update_rusage_metrics(metrics, rusage):
    fields = [
        'ru_utime',
        'ru_stime',
        'ru_maxrss',
        'ru_ixrss',
        'ru_idrss',
        'ru_isrss',
        'ru_minflt',
        'ru_majflt',
        'ru_nswap',
        'ru_inblock',
        'ru_oublock',
        'ru_msgsnd',
        'ru_msgrcv',
        'ru_nsignals',
        'ru_nvcsw',
        'ru_nivcsw',
    ]
    for k, v in zip(fields, rusage):
        metrics[k] = v


def read_head_tail(filename, length):
    with io.open(filename, errors='ignore', encoding='utf-8') as afile:
        afile.seek(0, os.SEEK_END)
        size = afile.tell()
        afile.seek(0, os.SEEK_SET)
        data = afile.read(length)
        pos = size - length * 2
        if pos > 0:
            afile.seek(pos + length, os.SEEK_SET)
            return data + "\n...\n" + afile.read()
        return data + afile.read()


def get_output_dir(work_dir):
    out_dir = os.path.join(work_dir, const.TESTING_OUT_DIR_NAME)
    exts.fs.ensure_dir(out_dir)
    return out_dir


def get_output_path(work_dir, path=""):
    return os.path.join(get_output_dir(work_dir), path)


def create_dirs(dirs):
    for d in dirs:
        exts.fs.ensure_dir(d)


def is_locale_supported(name):
    try:
        locale.setlocale(locale.LC_ALL, name)
        return True
    except locale.Error:
        return False


def get_test_work_dir(params):
    return test_common.get_test_suite_work_dir(
        params.build_root,
        params.project_path,
        params.test_suite_name,
        params.retry,
        split_count=params.split_count,
        split_index=params.split_index,
        target_platform_descriptor=params.target_platform_descriptor,
        split_file=params.split_file,
        multi_target_platform_run=params.multi_target_platform_run,
        remove_tos=params.remove_tos,
    )


def create_empty_outputs(params, overwrite=True):
    # create node's output files
    mode = 'w' if overwrite else 'a'
    for filename in [
        params.fast_clang_coverage_merge,
        params.log_path,
        params.meta,
        params.stderr,
        params.stdout,
        params.trace_output_filename,
    ]:
        if filename:
            exts.fs.ensure_dir(os.path.dirname(filename))
            open(filename, mode).close()

    # create node's output archives
    for filename in [
        params.allure,
        params.cpp_coverage_path,
        params.go_coverage_path,
        params.java_coverage_path,
        params.nlg_coverage_path,
        params.python3_coverage_path,
        params.ts_coverage_path,
        params.tar,
    ]:
        if filename and (overwrite or not os.path.exists(filename)):
            exts.fs.ensure_dir(os.path.dirname(filename))
            exts.archive.create_tar([], filename)


def process_execute(tags, stage, rs, *args, **kwargs):
    if 'ya:sys_info' not in tags:
        return process.execute(*args, **kwargs)

    def safe_call(f):
        try:
            f()
        except Exception:
            logger.exception("Exception while dumping si")

    cmd = kwargs['command']

    def dump_before():
        with open(rs.get_log_name(stage, cmd), "w") as f:
            f.write(system_info.get_system_info())

    def dump_after():
        with open(rs.get_log_name(stage, cmd), "w") as f:
            f.write("CMD: {}\n\n".format(cmd))
            f.write(system_info.get_system_info())

    if rs.stage == 0:
        safe_call(dump_before)

    try:
        return process.execute(*args, **kwargs)
    finally:
        safe_call(dump_after)


def become_subreaper():
    try:
        from library.python.prctl import prctl

        prctl.set_child_subreaper(1)

        # Fork and utilize zombies avoiding stealing return code of the wrapper executed by test_test
        pid = os.fork()
        if pid:
            while True:
                try:
                    p, status = os.waitpid(-1, 0)
                    if p == pid:
                        if os.WIFSIGNALED(status):
                            os.kill(os.getpid(), os.WTERMSIG(status))
                        else:
                            os._exit(os.WEXITSTATUS(status))
                except OSError as e:
                    if e.errno == errno.ECHILD:
                        break
                except BaseException:
                    time.sleep(0.1)

            os._exit(55)
        else:
            prctl.set_pdeathsig(signal.SIGTERM)
            return None
    except Exception as e:
        return e


def extract_param_value(cmd, prefix):
    prefix += '='
    for x in cmd:
        if x.startswith(prefix):
            return x[len(prefix) :]


def restore_chunk_identity(suite, opts):
    if opts.split_count:
        suite.chunk.nchunks = opts.split_count
        suite.chunk.chunk_index = opts.split_index
    if opts.split_file:
        suite.chunk.filename = opts.split_file


def read_tail(filename):
    if filename and os.path.exists(filename):
        return read_head_tail(filename, const.REPORT_SNIPPET_LIMIT)


def setup_namespaces(options):
    flags = 0
    mapuser = os.geteuid()
    mapgroup = os.getegid()

    if const.YaTestTags.MapRootUser in options.test_tags:
        flags |= unshare.CLONE_NEWUSER
        mapuser = mapgroup = 0

    if flags:
        logger.debug("Setup namespaces: flags:%s mapuser:%s mapgroup:%s", flags, mapuser, mapgroup)
        unshare.unshare_ns(flags, mapuser, mapgroup)


def setup_ram_drive(env, options, cmd):
    ram_drive_path, private_ram_drive = None, False

    if options.requires_ram_disk:
        for name, is_private in [
            # Sandbox provides one tmpfs mount point for task and it will be used by all nodes
            ('YA_RAM_DRIVE_PATH', False),
            # Distbuild provides separate tmpfs mount points for every node
            ('DISTBUILD_RAM_DISK_PATH', True),
        ]:
            if name in env:
                ram_drive_path = env[name]
                logger.debug("Using ram drive specified in the '%s' evn.var: %s", name, ram_drive_path)
                private_ram_drive = is_private
                # Backward compatibility - remove someday in one good thursday morning
                if options.supports_test_parameters:
                    cmd += ["--test-param", "ram_drive_path=" + ram_drive_path]
                break

    # Mine ram drive path from test parameters provided by user
    if not ram_drive_path:
        ram_drive_path = extract_param_value(cmd, "ram_drive_path")
        if ram_drive_path:
            logger.debug("Using ram drive specified by 'ram_drive_path' test parameter: %s", ram_drive_path)

    # create new namespace and mount tmpfs
    if options.local_ram_drive_size:
        if tmpfs.is_mount_supported():
            ram_drive_path = os.path.abspath("tmpfs4test")
            os.mkdir(ram_drive_path)
            tmpfs.mount_tempfs_newns(ram_drive_path, options.local_ram_drive_size * 1024)
            logger.debug("Using ram drive mounted into a new namespace: %s", ram_drive_path)
            private_ram_drive = True
        else:
            logger.warning("Can't mount tmpfs on the current platform")

    return ram_drive_path, private_ram_drive


def set_user_env_vars(env, env_data, global_resources):
    for entry in env_data:
        if '=' not in entry:
            continue
        key, value = entry.split("=", 1)
        value = '=' + value
        for prefix, res_name, suffix in set(re.findall('([=:;])(\\$[A-Za-z0-9_]+_RESOURCE_GLOBAL)(/)', value)):
            for k, v in global_resources.items():
                if k == res_name[1:]:
                    value = value.replace(prefix + res_name + suffix, prefix + v + suffix)
                    continue
        env[key] = value[1:]


def update_chunk_metrics(suite, stages, main_start_timestamp):
    test_cases_duration = sum(t.elapsed for t in suite.chunk.tests)
    first_test_case_ts = min((t.started for t in suite.chunk.tests if t.started), default=None)

    mil = 1e6
    bin_start_ts = os.getenv('_BINARY_START_TIMESTAMP')
    bin_start_ts = int(bin_start_ts) / mil if bin_start_ts else None

    bin_exec_ts = os.getenv('_BINARY_EXEC_TIMESTAMP') or os.getenv('DISTBUILD_RUNNER_BINARY_START_TIMESTAMP')
    bin_exec_ts = int(bin_exec_ts) / mil if bin_exec_ts else None

    if test_cases_duration:
        stages.set('in_test_secs', test_cases_duration)
        duration = stages.get_duration('wrapper_execution')
        if duration:
            stages.set('off_test_secs', duration - test_cases_duration)

    if bin_start_ts:
        stages.set('binary_startup_secs', main_start_timestamp - bin_start_ts)
    if bin_start_ts and bin_exec_ts:
        stages.set('binary_exec_delay_secs', bin_start_ts - bin_exec_ts)
    if first_test_case_ts and bin_start_ts:
        stages.set('delay_until_first_test_secs', first_test_case_ts - bin_start_ts)


def dump_slowest_tests(suite, limit=15):
    tests_info = [(str(test), test.elapsed) for test in suite.tests]
    if tests_info:
        tests_info = sorted(tests_info, key=lambda item: item[1], reverse=True)
        tests_info = tests_info[:limit]
        longest = max(int(math.log(math.ceil(time or sys.float_info.min), 10)) for _, time in tests_info)
        fraction = 2
        # better readability
        if longest == 0:
            longest = 4 + fraction
        else:
            longest += 3 + fraction
        pattern = "{time:%d.%df}s {name}" % (longest, fraction)
        lines = [pattern.format(time=x[1], name=x[0]) for x in tests_info]
        logger.debug("Slowest test cases:\n{}".format("\n".join(lines)))


def get_port_sync_dir(build_root):
    # Use value specified by local ya-bin by default
    env_name = 'PORT_SYNC_PATH'
    if env_name in os.environ:
        dirname = os.environ[env_name]
    else:
        dirname = os.path.join(build_root, 'port_sync_path')
    exts.fs.ensure_dir(dirname)
    return dirname


def get_recipe_name(cmd, i):
    return '{}-{}'.format(os.path.basename(cmd[0]), i)


def get_signal_name(signum):
    for sname, sval in signal.__dict__.items():
        if signum == sval:
            return sname


def is_test_tool(path):
    return os.path.basename(path) in ('test_tool', 'test_tool3')


def get_chunk_timeout_msg(user_timeout, actual_timeout, startup_delay):
    if user_timeout == actual_timeout:
        return "[[bad]]Chunk exceeded [[imp]]{}s[[bad]] timeout".format(actual_timeout)
    else:
        return "[[bad]]Chunk exceeded [[imp]]{}s[[bad]] timeout (user timeout: [[imp]]{}s[[bad]], YT op startup delay: [[imp]]{}s[[bad]])".format(
            actual_timeout, user_timeout, startup_delay
        )


def use_arg_file_if_possible(cmd, out_dir):
    if is_test_tool(cmd[0]) and (len(cmd) > 10 or sum(len(a) for a in cmd) > 8000):
        arg_file = os.path.join(out_dir, "test_tool.args")
        logger.debug("Pass parameters to test via file: %s", arg_file)
        with open(arg_file, "w") as f:
            f.writelines(c + "\n" for c in cmd[2:])
        return cmd[:2] + ["@" + arg_file]
    else:
        return cmd


def pstree_cmdline_limit(options):
    return None if const.YaTestTags.NoPstreeTrim in options.test_tags else PSTREE_CMDLINE_LIMIT


def main():
    subreaper_set_error = become_subreaper()
    is_subreaper_set = not bool(subreaper_set_error)

    main_start_timestamp = time.time()

    stages.set("start_timestamp", int(time.time()))
    stages.stage("initial")

    options = parse_args()
    cmd = options.command
    cwd = os.getcwd()

    # for test purpose
    if os.environ.get("TEST_YA_INTERNAL_CRASH_RUN_TEST"):
        exit(1)

    create_empty_outputs(options)
    # meta.json will be created right before correct node exit, otherwise we want to know about this problem which will lead to INTERNAL ERROR
    exts.fs.ensure_removed(options.meta)

    if options.trace_output_filename:
        try:
            from devtools.optrace.python import optrace

            optrace.trace_me(six.ensure_binary(options.trace_output_filename), append=True, files_in_report=-1)
        except ImportError:
            logger.debug("Output tracing disabled on this platform")

    devtools.ya.test.util.shared.setup_logging(options.log_level, options.log_path)
    logger.debug('Host %s, cwd %s, subreaper_set_error: %s', socket.gethostname(), cwd, subreaper_set_error)
    logger.debug(
        'Retry: %s; chunk %s (of %s); split file: %s',
        options.retry,
        options.split_index,
        options.split_count,
        options.split_file,
    )

    startup_delay = (
        int(time.time()) - int(os.environ.get("TEST_NODE_START_TIMESTAMP", 0))
        if "TEST_NODE_START_TIMESTAMP" in os.environ
        else 0
    )

    if options.node_timeout and not exts.windows.on_win():
        timeout = options.node_timeout - 10
        if startup_delay:
            stages.set("startup_delay", startup_delay)
            if startup_delay >= timeout:
                timeout = 1
            else:
                timeout -= startup_delay
        logger.debug('sigalarm_handler was set with timeout: %d', timeout)
        signal.signal(signal.SIGALRM, sigalarm_handler)
        signal.alarm(timeout)

    try:
        logger.debug('Available memory: %sM', devtools.ya.test.util.shared.get_available_memory_in_mb())
    except Exception as e:
        logger.debug('Available memory unknown: %s', e)

    env = system_env.Environ()
    # We can't redefine tmp env.vars because some tests would fail to create unix sockets
    # hitting filename length limit.
    env.adopt_update_mandatory(["TEMP", "TMP", "TMPDIR"])

    if options.dump_node_environment:
        stages.stage("dump_node_environment")
        shared.dump_dir_tree(root=options.build_root, header="Node environment:\n")

    list_path = options.test_list_path
    work_dir = get_test_work_dir(options)
    test_output_dir = os.path.join(work_dir, const.TESTING_OUT_DIR_NAME)
    trace_report = os.path.join(work_dir, const.TRACE_FILE_NAME)
    suite = generate_suite(options, work_dir)

    if list_path:
        cmd += ["--test-list-path", list_path]
        if not os.path.exists(list_path):
            suite.add_chunk_error("tests were not listed")
            suite.generate_trace_file(trace_report)
            return 0
        with open(list_path, 'r') as afile:
            test_list = json.load(afile)[int(options.split_index or 0)]
            if not test_list:
                with open(options.meta, 'w') as afile:
                    afile.write(const.NO_LISTED_TESTS)
                suite.generate_trace_file(trace_report)
                return 0
        logger.debug("test list: %s", test_list)
    else:
        logger.debug("tests were not listed before run test")

    cwd = os.getcwd()

    # If ram drive is requested, but it's not provided by environment it will be created inside a new namespace
    # which must be done before any thread is created and before tracing is initialized
    ram_drive_path, private_ram_drive = setup_ram_drive(env, options, cmd)

    if unshare.is_unshare_available():
        setup_namespaces(options)
    else:
        logger.debug("Setup namespaces disabled on this platform")

    if options.test_stdout:
        options.test_stderr = True

    exit_code = 0
    # Test wrapper exit status for test machinery
    exit_status = 0

    if not options.with_wine:
        for unicode_locale in ['C.UTF-8', 'en_US.UTF-8']:
            if is_locale_supported(unicode_locale):
                env['LC_ALL'] = unicode_locale
                break

    context = test_context.Context(options, work_dir, ram_drive_path)

    test_related_paths = options.test_related_paths or []

    # create an isolated environment
    source_root = options.source_root
    build_root = options.build_root
    data_root = options.data_root

    test_data_paths = options.test_data_paths or []
    for path in test_data_paths + test_related_paths:
        root_rel_path = os.path.relpath(path, options.source_root)
        # it's ok to not have fuzz data at first run
        if not os.path.exists(path) and not root_rel_path.startswith(const.CORPUS_DATA_ROOT_DIR):
            logger.warning(
                "{}: specified DATA '{}' doesn't exist".format(os.path.join(options.project_path, "ya.make"), path)
            )
    sandbox_resources = options.sandbox_resources
    external_local_files = options.external_local_files

    dirs = [work_dir, test_output_dir]
    if options.stderr:
        dirs += [os.path.dirname(options.stderr)]
    create_dirs(dirs)

    testing_finished = None
    stderr_path = options.stderr or get_output_path(work_dir, 'stderr')
    new_source_root = None

    mem_monitor = None
    tmpfs_monitor = None

    # [src] = dst. Src will be copied to the dst at test finalization stage.
    backup_map = {}
    command_cwd = None

    try:
        if os.environ.get("YA_TEST_FAIL_RUN_TEST"):
            raise Exception("As you wish")

        secret.start_test_tool_secret_server(env)

        if options.ram_limit_gb and not exts.windows.on_win():
            target_pid = os.getppid() if is_subreaper_set else os.getpid()
            precise_limit = options.ram_limit_gb * 1024**3
            # Take into account tmpfs
            if options.local_ram_drive_size:
                precise_limit -= options.local_ram_drive_size * 1024**3
            precise_limit = max([0, precise_limit])
            mem_monitor = monitor.MemProcessTreeMonitor(
                target_pid, precise_limit=precise_limit, delay=2, cmdline_limit=pstree_cmdline_limit(options)
            )
            mem_monitor.start()

        if private_ram_drive and monitor.is_stat_tmpfs_supported():
            tmpfs_monitor = monitor.TmpfsUsageMonitor(ram_drive_path, delay=1)
            tmpfs_monitor.start()

        stages.stage("setup_environment")
        resources_root = build_root
        # run_test node must use storages with use_cached_only=True, otherwise storage will try to download missing data
        # during test node's runtime, which is not allowed because:
        # - it should be cached (otherwise every run_test run would download data every time it runs)
        # - downloading would steal time from tests
        # - network might be not available (if no network:full requirement is specified)
        cached_storage = sandbox_storage.SandboxStorage(
            resources_root,
            use_cached_only=True,
            update_last_usage=False,
        )
        mds_canon_storage = mds_storage.MdsStorage(resources_root, use_cached_only=True)

        if options.create_clean_environment:
            new_source_root, _, new_data_root = testroot.create_environment(
                test_related_paths,
                test_data_paths,
                source_root,
                build_root,
                data_root,
                cwd,
                options.env_data_mode,
                options.create_root_guidance_file,
                options.pycache_prefix,
            )
        else:
            new_source_root = source_root
            new_data_root = data_root

        testroot.prepare_work_dir(
            build_root, work_dir, sandbox_resources, cached_storage, external_local_files, options.project_path
        )

        # change roots in the command arguments
        logger.debug("Changing roots in the test command %s: %s -> %s", cmd, source_root, new_source_root)
        for old_root, new_root in [(source_root, new_source_root)]:
            cmd = devtools.ya.test.util.shared.change_cmd_root(
                cmd, old_root, new_root, build_root, skip_list=[sys.argv[0]], skip_args=["--test-param"]
            )
        logger.debug("Changed command: %s", cmd)

        command_cwd = work_dir
        # change roots in test run cwd
        test_run_cwd = options.test_run_cwd
        if test_run_cwd:
            if not os.path.isdir(test_run_cwd):
                raise TestError("Specified TEST_CWD isn't directory or doesn't exist: {}".format(test_run_cwd))

            logger.debug("Changing test cwd: %s", test_run_cwd)
            for old_root, new_root in [(source_root, new_source_root)]:
                test_run_cwd = devtools.ya.test.util.shared.change_root(test_run_cwd, old_root, new_root, build_root)
            logger.debug("Changed test cwd: %s, %s", test_run_cwd, os.listdir(test_run_cwd))

        # change roots in the env's PYTHONPATH
        if options.setup_pythonpath_env:
            env = fix_python_path(
                env,
                test_related_paths + options.python_sys_paths,
                source_root,
                new_source_root,
                build_root,
                options.with_wine,
            )
            logger.debug("Changed python paths: %s", tools.get_python_paths(env))

        # set up coverage
        coverage = {}
        if options.java_coverage_path:
            coverage["java"] = {"output_file": options.java_coverage_path}
        if options.python3_coverage_path:
            coverage["python3"] = {"output_file": options.python3_coverage_path}
        if options.ts_coverage_path:
            coverage["ts"] = {"output_file": options.ts_coverage_path}
        if options.cpp_coverage_path:
            coverage["cpp"] = {"output_file": options.cpp_coverage_path}
        if options.go_coverage_path:
            coverage["go"] = {"output_file": options.go_coverage_path}
        if options.nlg_coverage_path:
            coverage["nlg"] = {"output_file": options.nlg_coverage_path}

        for cov_type, data in coverage.items():
            cov_dir = data["output_file"]
            assert cov_dir.endswith(".tar"), cov_dir
            cov_dir = cov_dir[:-4]
            data["output_dir"] = cov_dir
            if not os.path.exists(cov_dir):
                os.makedirs(cov_dir)
            logger.debug("Coverage %s dir: %s", cov_type, cov_dir)

        if coverage.get("go"):
            env.set_mandatory(const.COVERAGE_GO_ENV_NAME, "%s/cov_{pid}_{time}" % coverage["go"]["output_dir"])

        if coverage.get("python3"):
            path = coverage["python3"]["output_dir"]

            dirname, basename = os.path.split(path)
            assert basename.startswith("py3."), basename
            # Don't mix coverage from different pythons in one directory
            basename = basename.replace("py3.", "py{python_ver}.")
            env.set_mandatory(const.COVERAGE_PYTHON_ENV_NAME, "%s/%s/{bin}:py{python_ver}:cov" % (dirname, basename))
            env.set_mandatory(const.PYTHON_COVERAGE_PREFIX_FILTER_ENV_NAME, options.coverage_prefix_filter)
            env.set_mandatory(const.PYTHON_COVERAGE_EXCLUDE_REGEXP_ENV_NAME, options.coverage_exclude_regexp)

        if coverage.get("ts"):
            env.set_mandatory(const.COVERAGE_TS_ENV_NAME, coverage["ts"]["output_dir"])

        if options.sancov_coverage:
            envvar = "ASAN_OPTIONS"
            env.extend_mandatory(envvar, "coverage=1")
            env.extend_mandatory(envvar, "coverage_dir={}".format(coverage["cpp"]["output_dir"]))

        if options.clang_coverage:
            env.set_mandatory(
                const.COVERAGE_CLANG_ENV_NAME, os.path.join(coverage["cpp"]["output_dir"], "%e.%p.clang.profraw")
            )
            if options.fast_clang_coverage_merge:
                try:
                    from devtools.ya.test.programs.test_tool import cov_merge_vfs
                except ImportError as e:
                    raise Exception("No cov_merge_vfs for your platform: %s", e)

                logfile = options.fast_clang_coverage_merge
                exts.fs.ensure_dir(os.path.dirname(logfile))
                coverage["cpp"]["vfs"] = cov_merge_vfs.VFS()

                if options.node_timeout:
                    # We really really don't want to get inconsistent disconnected mount point on the distbuild.
                    # That's why we don't rely on successful completion of the node and always set auto_unmount_timeout
                    time_spent = int(time.time()) - int(main_start_timestamp)
                    auto_unmount_timeout = options.node_timeout - time_spent - 30
                else:
                    auto_unmount_timeout = 0

                coverage["cpp"]["vfs"].mount(
                    coverage["cpp"]["output_dir"],
                    logfile,
                    auto_unmount_timeout,
                    merge_policy=cov_merge_vfs.MergePolicy.Relaxed,
                )

        if coverage.get("nlg"):
            env.set_mandatory(const.COVERAGE_NLG_ENV_NAME, "{}/%p.nlgcov".format(coverage["nlg"]["output_dir"]))

        context.update(
            'runtime',
            {
                'atd_root': new_data_root,
                'source_root': new_source_root,
            },
        )

        # Always specify PORT_SYNC_PATH env.var. to make portmanagers select ports in cooperative manner inside test node
        env["PORT_SYNC_PATH"] = get_port_sync_dir(options.build_root)

        stderr = None
        stdout = None
        wrapper_stderr_tail = None

        test_command_retry_num = 0
        if options.node_timeout and startup_delay:
            test_timeout = min(options.node_timeout - startup_delay - 30, options.timeout)
            if test_timeout <= 0:
                raise TestRunTimeExhausted(
                    "No time left for testing - chunk run is aborted (startup delay: {}s)".format(startup_delay)
                )
        else:
            test_timeout = options.timeout
        run_timeout = test_timeout
        test_run_dirs = []
        timeout_callback = None

        if options.smooth_shutdown_signals and not exts.windows.on_win():
            logger.debug("Wrapper supports %s smooth shutdown signals", options.smooth_shutdown_signals)

            def timeout_callback(exec_obj, timeout):  # noqa: F811
                pid = exec_obj.process.pid

                def safe_kill(pid, sig):
                    try:
                        os.kill(pid, sig)
                    except Exception as e:
                        logger.debug("kill failed with: %s", e)

                stages.stage("timeout_processing")
                logger.warning("Wrapper execution timed out")

                mon = monitor.MemProcessTreeMonitor(exec_obj.process.pid, cmdline_limit=pstree_cmdline_limit(options))
                proc_tree_str = mon.poll().dumps_process_tree().strip()
                logger.warning(
                    "Wrapper has overrun %s secs timeout. Process tree before termination:\n%s", timeout, proc_tree_str
                )

                for sig in options.smooth_shutdown_signals:
                    logger.debug("Sending %s to the wrapper (%d)", sig, pid)
                    safe_kill(pid, getattr(signal, sig))
                try:
                    process.wait_for(lambda: not exec_obj.running, timeout=SMOOTH_SHUTDOWN_TIMEOUT, sleep_time=0.5)
                    return
                except process.TimeoutError:
                    logger.debug("Wrapper failed to shutdown gracefully in %d seconds", SMOOTH_SHUTDOWN_TIMEOUT)

                logger.debug("Sending %s to the wrapper (%d)", TIMEOUT_KILL_SIGNAL, pid)
                safe_kill(pid, TIMEOUT_KILL_SIGNAL)
                try:
                    process.wait_for(lambda: not exec_obj.running, timeout=15, sleep_time=0.5)
                    return
                except process.TimeoutError:
                    logger.debug("Wrapper failed to quit in %d seconds", 15)

        command_cwd = test_run_cwd or work_dir

        if options.with_wine:
            wineprefix = os.path.join(work_dir, ".wine")
            os.mkdir(wineprefix)
            env["WINEPREFIX"] = wineprefix

        user_env = {}
        set_user_env_vars(user_env, options.test_env, options.global_resources)
        env.update(user_env)

        context.update_env(user_env)

        if context.get("build", "sanitizer") and "--binary" in cmd:
            binary_path = cmd[cmd.index("--binary") + 1]
            env["DYLD_LIBRARY_PATH"] = os.path.dirname(binary_path)

        failed_recipes = []

        if options.prepare_only:
            test_context_file_path = os.path.join(work_dir, const.SUITE_CONTEXT_FILE_NAME)
            context.save(test_context_file_path)
            logger.debug("Saved test context to %s", test_context_file_path)

        else:
            while need_to_rerun_test(
                stderr, exit_code, test_command_retry_num, options.test_tags, failed_recipes, options.test_size
            ):
                if test_command_retry_num > 0:
                    stages.stage("retry_preparations")
                    logger.debug("Running %d test attempt", test_command_retry_num + 1)
                    run_timeout = int(main_start_timestamp + test_timeout - time.time() + 0.5)
                    if run_timeout <= 0:
                        # no time left for run
                        break
                    renamed_work_dir = "{}_try_{}".format(work_dir, test_command_retry_num - 1)
                    move_test_work_dir(work_dir, renamed_work_dir, cmd)
                    test_run_dirs.append(renamed_work_dir)

                    logger.info(
                        "Test will be restarted with timeout %s, logs of the previous retry are saved to %s",
                        run_timeout,
                        renamed_work_dir,
                    )

                out_dir = get_output_path(work_dir)

                stderr_path = options.stderr or get_output_path(work_dir, 'stderr')
                stdout_path = options.stdout or get_output_path(work_dir, 'stdout')

                wrapper_stdout = open(stdout_path, 'w')
                wrapper_stderr = open(stderr_path, 'w')

                # Use ram drive if provided
                if ram_drive_path:
                    import uuid

                    output_ram_drive_path = os.path.join(
                        ram_drive_path, "run_test_{}".format(uuid.uuid4()), const.TESTING_OUT_RAM_DRIVE_DIR_NAME
                    )
                    backup_map[output_ram_drive_path] = os.path.join(out_dir, const.TESTING_OUT_RAM_DRIVE_DIR_NAME)
                    exts.fs.ensure_dir(output_ram_drive_path)
                    context.set('runtime', 'test_output_ram_drive_path', output_ram_drive_path)
                if "HDD_PATH" in os.environ:
                    context.set('runtime', 'yt_hdd_path', os.environ["HDD_PATH"])
                    del os.environ["HDD_PATH"]
                # Pass test context to recipes and test's wrapper
                env.set_mandatory('YA_TEST_CONTEXT_FILE', context.save())

                trace_watcher = TraceFileWatcher(
                    trace_report,
                    command_cwd if options.show_test_cwd else None,
                    options.output_style,
                    options.test_stderr,
                )
                watchers = [trace_watcher]
                recipe_process_listener = None
                # Don't try to monitor test's stderr if gdb_debug is requested
                # Stderr with point to the tty and so will be printed
                if options.test_stderr and not options.gdb_debug:
                    stderr_watcher = StdErrWatcher()
                    watchers.append(stderr_watcher)
                    recipe_process_listener = stderr_watcher

                if options.show_test_cwd:
                    import app_ctx

                    app_ctx.display.emit_message("##Tests run working directory: {}\n".format(command_cwd))

                try:
                    recipes_env_file = os.path.join(work_dir, "env.json.txt")
                    rs = RunStages(options)
                    if options.recipes:
                        stages.stage("prepare_recipes")
                        for i, recipe in enumerate(dartfile.decode_recipe_cmdline(options.recipes), start=1):
                            recipe_cmd = extend_recipe_cmd(recipe, options, cmd, "start", recipes_env_file)
                            recipe_name = get_recipe_name(recipe_cmd, i)
                            recipe_err_filename = os.path.join(out_dir, "recipe_start_{}.err".format(recipe_name))
                            recipe_out_filename = os.path.join(out_dir, "recipe_start_{}.out".format(recipe_name))
                            try:
                                with reserve_disk_space(options.space_to_reserve, stages):
                                    process_execute(
                                        options.test_tags,
                                        RunStages.TST_START_RECP,
                                        rs,
                                        command=recipe_cmd,
                                        env=env.dump(),
                                        wait=True,
                                        cwd=command_cwd,
                                        stderr=recipe_err_filename,
                                        stdout=recipe_out_filename,
                                        process_progress_listener=recipe_process_listener,
                                        create_new_process_group=(not options.same_process_group),
                                        stdout_to_stderr=options.test_stdout,
                                    )
                            except Exception as e:
                                logger.exception("Recipe %s start up exception: %s", recipe_name, e)
                                with open(recipe_err_filename, 'a') as afile:
                                    afile.write("\nRecipe start up exception:\n")
                                    traceback.print_exc(file=afile)
                                raise RecipeStartUpError(recipe_name, recipe_err_filename, recipe_out_filename)

                            env.update(get_recipes_env(recipes_env_file))

                    logger.debug(
                        "Executing test cmd: '%s' with timeout %d, with env: %s",
                        " ".join(cmd),
                        run_timeout,
                        json.dumps(env.dump(safe=True), indent=4, sort_keys=True),
                    )

                    if "TEST_COMMAND_WRAPPER" in env:
                        cmd = shlex.split(env["TEST_COMMAND_WRAPPER"]) + cmd

                    create_new_process_group = (
                        not options.same_process_group and "DONT_CREATE_TEST_PROCESS_GROUP" not in env
                    )

                    if options.dump_test_environment:
                        stages.stage("dump_test_environment")
                        shared.dump_dir_tree(root=options.build_root)

                    if options.gdb_debug and not is_test_tool(cmd[0]):
                        exec_func = run_with_gdb(options.gdb_path, "/dev/tty", process_execute, source_root)
                    else:
                        exec_func = process_execute
                    with reserve_disk_space(options.space_to_reserve, stages):
                        stages.stage("wrapper_execution")
                        modded_cmd = use_arg_file_if_possible(cmd, out_dir)
                        res = exec_func(
                            options.test_tags,
                            RunStages.TST_RUN_TEST,
                            rs,
                            command=modded_cmd,
                            cwd=command_cwd,
                            env=env.dump(),
                            wait=False,
                            stderr=wrapper_stderr,
                            stdout=wrapper_stdout,
                            process_progress_listener=CompositeProcessWatcher(watchers),
                            create_new_process_group=create_new_process_group,
                            stdout_to_stderr=options.test_stdout,
                        )

                        if options.show_test_pid:
                            trace_watcher.pid = res.process.pid

                        res.wait(check_exit_code=False, timeout=run_timeout, on_timeout=timeout_callback)
                    exit_code = res.exit_code
                    exit_status = exit_code

                except process.ExecutionTimeoutError as e:
                    testing_finished = time.time()

                    exit_code = e.execution_result.exit_code
                    exit_status = const.TestRunExitCode.TimeOut

                    wrapper_stderr_tail = read_tail(stderr_path).strip()

                    if coverage.get("cpp"):
                        sys.stderr.write(
                            "Test [{}] crashed by timeout, use --test-disable-timeout option\n".format(
                                options.project_path
                            )
                        )

                    trace = traceback.format_exc()
                    wrapper_stderr.write("\n" + trace)
                    wrapper_stdout.write("\n" + trace)
                    break
                finally:
                    stages.flush()

                    if not wrapper_stderr_tail:
                        wrapper_stderr_tail = read_tail(stderr_path)

                    if options.recipes:
                        stages.stage("stop_recipes")
                        for i, recipe in reversed(
                            list(enumerate(dartfile.decode_recipe_cmdline(options.recipes), start=1))
                        ):
                            recipe_cmd = extend_recipe_cmd(recipe, options, cmd, "stop", recipes_env_file)
                            recipe_name = get_recipe_name(recipe_cmd, i)
                            recipe_err_filename = os.path.join(out_dir, "recipe_stop_{}.err".format(recipe_name))
                            recipe_out_filename = os.path.join(out_dir, "recipe_stop_{}.out".format(recipe_name))
                            try:
                                with reserve_disk_space(options.space_to_reserve, stages):
                                    process_execute(
                                        options.test_tags,
                                        RunStages.TST_STOP_RECP,
                                        rs,
                                        command=recipe_cmd,
                                        env=env.dump(),
                                        wait=True,
                                        cwd=command_cwd,
                                        stderr=recipe_err_filename,
                                        stdout=recipe_out_filename,
                                        process_progress_listener=recipe_process_listener,
                                        create_new_process_group=(not options.same_process_group),
                                        stdout_to_stderr=options.test_stdout,
                                    )
                            except Exception as e:
                                logger.exception("Recipe %s tear down exception: %s", recipe_name, e)
                                with open(recipe_err_filename, 'a') as afile:
                                    afile.write("\nRecipe tear down exception:\n")
                                    traceback.print_exc(file=afile)
                                failed_recipes.append(
                                    RecipeTearDownError(recipe_name, recipe_err_filename, recipe_out_filename)
                                )

                            env.update(get_recipes_env(recipes_env_file))

                    wrapper_stdout.close()
                    wrapper_stderr.close()

                    if stdout_path:
                        stdout = read_head_tail(stdout_path, const.REPORT_SNIPPET_LIMIT)
                    if stderr_path:
                        stderr = read_head_tail(stderr_path, const.REPORT_SNIPPET_LIMIT)

                    if not options.test_stderr and stderr:
                        msg_lines = test_common.to_utf8(stderr).strip(" \n").splitlines()
                        first_text = True
                        for line in msg_lines:
                            if any(p in line for p in ("fixme:toolhelp", "fixme:module:load_library")):
                                continue
                            if not line.startswith('##') and first_text:
                                first_text = False
                                sys.stderr.write("Test command err:\n")
                            sys.stderr.write(line)
                            sys.stderr.write("\n")

                stages.set("retries", test_command_retry_num)
                test_command_retry_num += 1

        # moving all test run logs to the final test output dir
        for d in test_run_dirs:
            exts.fs.move(d, os.path.join(out_dir, os.path.basename(d)))

        if not testing_finished:
            testing_finished = time.time()

        if os.path.exists(options.trace_wreckage):
            logger.debug("Wreckage tracefile found: %s", options.trace_wreckage)
            with open(options.trace_wreckage) as src, open(trace_report, 'a') as dst:
                dst.write('\n')
                shutil.copyfileobj(src, dst)

        if options.store_original_tracefile_tar:
            stages.stage("store_original_tracefile")
            with open(trace_report) as src, open(options.store_original_tracefile_tar, 'w') as dst:
                shutil.copyfileobj(src, dst)

        stages.stage("load_results")
        suite.load_run_results(trace_report)
        if suite.get_status == const.Status.GOOD and not suite.tests:
            logger.debug("Test wrapper didn't execute any test")

        stages.stage("process_results")
        if suite.chunks:
            assert len(suite.chunks) == 1, suite.chunks
            # Test wrapper doesn't operate with tracefile in terms of specific chunks.
            # Set chunk info (if required) to generate tracefile with all required
            # meta info for proper merge in following nodes
            restore_chunk_identity(suite, options)
        else:
            # Trace file might be empty in case of errors or presence of the skipped tests
            register_chunk(suite, options)

        suite_error_status = None
        dump_wrapper_stderr = False

        if exit_code == -TIMEOUT_KILL_SIGNAL:
            suite_error_status = const.Status.TIMEOUT
            msg = "{prefix}, failed to shutdown gracefully in [[imp]]{stimeout}s[[bad]] and was terminated using {sig} signal".format(
                prefix=get_chunk_timeout_msg(options.timeout, test_timeout, startup_delay),
                stimeout=SMOOTH_SHUTDOWN_TIMEOUT,
                sig='SIGQUIT',
            )
            suite.add_chunk_error(msg, suite_error_status)
            logger.debug(strip_markup(msg))
        elif exit_status == const.TestRunExitCode.TimeOut:
            suite_error_status = const.Status.TIMEOUT
            msg = "{} and was killed".format(get_chunk_timeout_msg(options.timeout, test_timeout, startup_delay))
            suite.add_chunk_error(msg, suite_error_status)
            logger.debug(strip_markup(msg))
        elif exit_code != 0:
            suite_error_status = const.Status.FAIL
            if exit_code > 0 or exts.windows.on_win():
                suite.add_chunk_error(
                    "[[bad]]Test failed with [[imp]]{}[[bad]] exit code. See logs for more info".format(exit_code),
                    suite_error_status,
                )
            else:
                signum = -exit_code
                suite.add_chunk_error(
                    "[[bad]]Test process was killed by [[imp]]{}[[bad]] signal. See logs for more info".format(
                        get_signal_name(signum)
                    ),
                    suite_error_status,
                )
            dump_wrapper_stderr = True

        devtools.ya.test.util.shared.adjust_test_status(
            suite,
            exit_status,
            testing_finished,
            run_timeout,
            test_command_retry_num,
            stdout,
            stderr,
            suite.add_chunk_error,
            options.propagate_timeout_info,
        )
        update_chunk_logs(suite, options)
        update_chunk_metrics(suite, stages, main_start_timestamp)
        dump_slowest_tests(suite)

        if suite_error_status and exit_code < 0:
            logger.debug("Trying to recover dump core file")
            filename = os.path.basename(cmd[0])
            out_dir = get_output_path(work_dir)
            bt = shared.postprocess_coredump(
                cmd[0],
                command_cwd,
                res.process.pid,
                suite.chunk.logs,
                options.gdb_path,
                not options.truncate_files,
                filename,
                out_dir,
            )
            if bt:
                suite.add_chunk_error("[[bad]]{}".format(cores.colorize_backtrace(bt)), suite_error_status)

        if dump_wrapper_stderr and wrapper_stderr_tail:
            logger.debug("Dumping wrapper's stderr tail")
            tail = six.ensure_text(wrapper_stderr_tail)[-500:].strip()
            suite.add_chunk_error(
                "[[imp]]Test wrapper's stderr tail:[[bad]]\n{}".format(tail),
                suite_error_status,
            )

        if failed_recipes:
            if len(failed_recipes) == 1:
                err = failed_recipes[0]
                suite.add_chunk_error(err.format_full())
                suite.chunk.logs.update(
                    {
                        'recipe stderr': err.err_filename,
                        'recipe stdout': err.out_filename,
                    }
                )
            else:
                for err in failed_recipes:
                    suite.add_chunk_error("{}. See recipe's stderr for more info".format(err.format_info()))
                    suite.chunk.logs.update(
                        {
                            'recipe {} stderr'.format(err.name): err.err_filename,
                            'recipe {} stdout'.format(err.name): err.out_filename,
                        }
                    )

        try:
            import resource

            update_rusage_metrics(suite.chunk.metrics, resource.getrusage(resource.RUSAGE_CHILDREN))
        except ImportError:
            logger.debug("resource module is unavailable")

        core_search_file = context.get('internal', 'core_search_file')
        if core_search_file and os.path.exists(core_search_file):
            from . import core_file

            stages.stage("process_user_cores")

            core_file.process_user_cores(
                core_search_file,
                suite,
                options.gdb_path,
                get_output_path(work_dir),
                core_limit=2,
                store_cores=options.store_cores,
            )

        stop_monitors(
            mem_monitor,
            options.ram_limit_gb,
            tmpfs_monitor,
            suite,
            options.autocheck_mode,
            options.disable_memory_snippet,
        )

        if suite.chunk.tests:
            if options.tests_limit_in_chunk:
                # Rough barrier to prevent flooding CI with generated test cases
                # For more info see DEVTOOLSSUPPORT-55650
                shared.limit_tests(suite.chunk, limit=options.tests_limit_in_chunk)

            if options.supports_canonization and any(t.result is not None for t in suite.chunk.tests):
                import devtools.ya.test.canon.data as canon_data

                stages.stage("canonical_data_verification")
                canonical_data = canon_data.CanonicalData(
                    arc_path=options.custom_canondata_path or options.source_root,
                    sandbox_storage=cached_storage,
                    resource_owner=options.result_resource_owner,
                    ssh_keys=options.result_resource_owner_key,
                    resource_ttl=options.result_resource_ttl,
                    max_file_size=options.result_max_file_size,
                    max_inline_diff_size=options.max_inline_diff_size,
                    max_str_len=sys.maxint if six.PY2 else sys.maxsize,  # don't extract stings at the run_test stage
                    mds_storage=mds_canon_storage,
                    sub_path=options.sub_path,
                )

                if options.verify:
                    canonical_data.verify(suite)
                else:
                    canonical_data.save(suite)

        if options.replace_roots:
            stages.stage("replace_roots")
            replace_roots(suite, new_source_root, command_cwd, build_root)

        # XXX DEVTOOLS-4273
        if coverage.get("java"):
            validate_java_coverage(coverage["java"]["output_dir"])
    except Exception as exc:
        logger.exception("%r Internal error occurred", exc)
        exts.fs.ensure_dir(os.path.dirname(stderr_path))
        with open(stderr_path, 'a') as afile:
            traceback.print_exc(file=afile)

        suite = generate_suite(options, work_dir)
        suite.set_work_dir(work_dir)
        update_chunk_logs(suite, options)

        is_user_error = True
        if isinstance(exc, RecipeError):
            error_msg = exc.format_full()
            suite.chunk.logs.update(
                {
                    'recipe stderr': exc.err_filename,
                    'recipe stdout': exc.out_filename,
                }
            )
        elif isinstance(exc, (testroot.ResourceConflictException, TestError)):
            error_msg = str(exc)
        elif isinstance(exc, IOError) and getattr(exc, 'errno', None) == errno.ENOMEM:
            # IOError: [Errno 12] Cannot allocate memory
            error_msg = "run_test maxrss: {} Kb\nMost likely you need to specify more RAM requirement".format(
                runtime.get_maxrss()
            )
        else:
            error_msg = traceback.format_exc()
            is_user_error = False

        if is_user_error:
            status = const.Status.FAIL
            test_rc = const.TestRunExitCode.Failed
        else:
            status = const.Status.INTERNAL
            test_rc = const.TestRunExitCode.InfrastructureError

        suite.add_chunk_error("[[bad]]{}".format(error_msg), status)
        suite.chunk.logs.update({'stderr': stderr_path})

        if options.replace_roots and new_source_root and command_cwd:
            stages.stage("replace_roots")
            replace_roots(suite, new_source_root, command_cwd, build_root)

        stop_monitors(
            mem_monitor,
            options.ram_limit_gb,
            tmpfs_monitor,
            suite,
            options.autocheck_mode,
            options.disable_memory_snippet,
        )

        finalize(options, suite, work_dir, test_rc, main_start_timestamp, time.time(), {}, backup_map)
        return get_exit_code(suite)

    finalize(options, suite, work_dir, exit_status, main_start_timestamp, testing_finished, coverage, backup_map)
    return get_exit_code(suite)


def get_exit_code(suite):
    # Distbuild wants to know if there were fails when resources for the test were deflated (stat.scheduler logic)
    if os.environ.get('DISTBUILD_DEFLATED_REQS_RUN', '0') in ['1', 'yes']:
        return (
            0
            if suite.get_status()
            in {
                devtools.ya.test.common.Status.GOOD,
                devtools.ya.test.common.Status.SKIPPED,
                devtools.ya.test.common.Status.DESELECTED,
            }
            else TESTS_FAILED_EXIT_CODE
        )
    return 0


def stop_monitors(mem_monitor, ram_limit_gb, tmpfs_monitor, suite, autocheck_mode=False, disable_snippet=False):
    procmem, tmpfsmem = 0, 0

    if mem_monitor and mem_monitor.is_alive():
        mem_monitor.stop()
        procmem = mem_monitor.get_max_mem_used()

    if tmpfs_monitor and tmpfs_monitor.is_alive():
        tmpfs_monitor.stop()
        tmpfsmem = tmpfs_monitor.get_max_mem_used()

    total = procmem + tmpfsmem

    if not total:
        return

    if tmpfsmem:
        logger.debug(
            "Maximum memory consumption of the tmpfs: %sK (max inodes: %d)",
            tmpfsmem,
            tmpfs_monitor.get_max_inodes_used(),
        )
        stages.set("max_tmpfs_memory_consumption_kb", tmpfsmem)
        stages.set("max_tmpfs_inodes", tmpfs_monitor.get_max_inodes_used())

    if procmem:
        proc_tree_str = mem_monitor.dumps_process_tree()
        logger.debug("Maximum memory consumption of the process tree: %sK\n%s", procmem, proc_tree_str)
        stages.set("max_proc_tree_memory_consumption_kb", procmem)
    else:
        proc_tree_str = None

    ram_limit_kb = ram_limit_gb * 1024 * 1024
    # For more info see https://st.yandex-team.ru/DEVTOOLS-5548#5d31af2163890d001c30ad84
    if not disable_snippet and ram_limit_gb and total > ram_limit_kb:
        parts = [
            "[[bad]]Test run has exceeded [[imp]]{}[[bad]] ({}K) memory limit with [[imp]]{}[[bad]] ({}K) used.[[rst]] This may lead to test failure on the Autocheck/CI".format(
                formatter.format_size(ram_limit_kb * 1024),
                ram_limit_kb,
                formatter.format_size(total * 1024),
                total,
            )
        ]
        if tmpfsmem:
            parts.append(" tmpfs used: [[imp]]{}[[rst]]".format(formatter.format_size(tmpfsmem * 1024)))
            if procmem:
                parts.append(" process tree used: [[imp]]{}[[rst]]".format(formatter.format_size(procmem * 1024)))
        parts.append("You can increase test's ram requirement using [[imp]]REQUIREMENTS(ram:X)[[rst]] in the ya.make")
        if proc_tree_str:
            parts.append(proc_tree_str)
        if autocheck_mode:
            suite.add_chunk_error('\n'.join(parts), devtools.ya.test.common.Status.FAIL)
        else:
            suite.add_chunk_info('\n'.join(parts))


def validate_java_coverage(dirname):
    for filename in os.listdir(dirname):
        filename = os.path.join(dirname, filename)
        # load_report will fail if content isn't valid
        try:
            lib_coverage.jacoco_report.load_report(filename)
        except Exception:
            msg = (
                "Java coverage ({}) corrupted. It may happen when jacoco's thread get killed"
                " while it was dumping coverage in the shutdown hook registered via addShutdownHook().\n"
                "Take a look at test and check there are no exceptions or halt() calls in your shutdown hooks".format(
                    filename
                )
            )
            ei = sys.exc_info()
            six.reraise(ei[0], "{}\n{}".format(ei[1], msg), ei[2])


def dump_meta(params, test_rc, start_time, end_time):
    meta = {
        'exit_code': test_rc,
        'elapsed': end_time - start_time,
        'start_time': datetime.datetime.fromtimestamp(start_time).strftime(const.TIMESTAMP_FORMAT),
        'end_time': datetime.datetime.fromtimestamp(end_time).strftime(const.TIMESTAMP_FORMAT),
        'cwd': os.getcwd(),
        'env_build_root': params.build_root,
        'name': params.test_suite_name,
        'project': params.project_path,
        'test_timeout': params.timeout,
        'test_size': params.test_size,
        'test_tags': params.test_tags,
        'test_type': params.test_type,
        'test_ci_type': params.test_ci_type,
        'target_platform_descriptor': params.target_platform_descriptor,
        'multi_target_platform_run': params.multi_target_platform_run,
    }

    with open(params.meta, 'w') as meta_file:
        meta_file.write(json.dumps(meta, indent=4))


def finalize_node(suite):
    # Generate json mini report with chunk results for Distbuild
    if const.DISTBUILD_STATUS_REPORT_ENV_NAME in os.environ:
        filename = os.environ[const.DISTBUILD_STATUS_REPORT_ENV_NAME]
        logger.debug("Saving chunk report to %s", filename)
        chunk_result.generate_report(suite.chunk, filename)


def finalize(options, suite, work_dir, test_rc, start_time, end_time, coverage, backup_map):
    out_dir = os.path.join(work_dir, const.TESTING_OUT_DIR_NAME)
    trace_report = os.path.join(work_dir, const.TRACE_FILE_NAME)
    if options.stdout:
        open(options.stdout, "a").close()
    if options.stderr:
        open(options.stderr, "a").close()

    if backup_map:
        stages.stage("backup")
        for src, dst in backup_map.items():
            logger.debug("Backup {} to {}\n".format(src, dst))

            def ignore(path, names):
                ignored = set()
                for name in names:
                    st = os.lstat(os.path.join(path, name))
                    if not stat.S_ISREG(st.st_mode) and not stat.S_ISDIR(st.st_mode):
                        ignored.add(name)
                return ignored

            def safe_copyfile(src, dst):
                try:
                    shutil.copy2(src, dst)
                except Exception as e:
                    logger.debug("Failed to copy {} -> {}: {}".format(src, dst, e))

            exts.fs.copytree3(src, dst, copy_function=safe_copyfile, ignore=ignore, ignore_dangling_symlinks=True)

    postprocess_outputs(
        suite, options.test_tags, out_dir, options.truncate_files, options.truncate_files_limit, options.keep_symlinks
    )

    if options.meta:
        dump_meta(options, test_rc, start_time, end_time)

    def onerror_skip(src, dst, exc_info):
        logger.debug("Failed to add %s to archive: %s", src, exc_info[1])

    archive_postprocess = (
        None
        if options.keep_temps or options.should_tar_dir_outputs
        else devtools.ya.test.util.shared.archive_postprocess_unlink_files
    )

    for cov_type, data in coverage.items():
        stages.stage("tar_{}_coverage".format(cov_type))

        output_dir = data["output_dir"]
        arch_path = output_dir + '.archive'
        exts.archive.create_tar(output_dir, arch_path, onerror=onerror_skip)
        # unmount vfs after reading data
        if "vfs" in data:
            data["vfs"].unmount()

        exts.fs.move(arch_path, data["output_file"])
        exts.fs.ensure_removed(output_dir)

    to_archive = [
        (os.path.join(work_dir, const.GENERATED_CORPUS_DIR_NAME), options.fuzz_corpus_tar, False),
        (os.path.join(work_dir, 'allure'), options.allure, False),
    ]
    if not options.dir_outputs or options.should_tar_dir_outputs:
        to_archive.append((out_dir, options.tar, True))
    for target, filename, compressible in to_archive:
        if not filename:
            continue

        if not os.path.exists(target):
            logger.debug("Path doesn't exist: %s (skip archiving)", target)
            continue

        stages.stage("tar_{}".format(os.path.basename(filename)))
        logger.debug('Tar directory %s to %s', target, filename)
        if compressible:
            compression_filter = options.compression_filter
            compression_level = options.compression_level
        else:
            compression_filter, compression_level = None, None
        exts.archive.create_tar(
            target,
            filename,
            compression_filter,
            compression_level,
            onerror=onerror_skip,
            postprocess=archive_postprocess,
        )
        suite.chunk.metrics["{}_size_in_kb".format(os.path.basename(filename))] = os.path.getsize(filename) // 1024

    stages.set("finish_timestamp", int(time.time()))
    suite.chunk.metrics.update(stages.dump())
    suite.chunk.metrics['wall_time'] = time.time() - start_time

    for testcase in suite.chunk.tests:
        testcase.metrics['elapsed_time'] = testcase.elapsed

    finalize_node(suite)

    stages.flush()
    stages.stage("generate_trace_file")
    suite.generate_trace_file(trace_report)
    stages.flush()
    logger.debug("Stages:\n{}".format(json.dumps(stages.dump(), indent=4, sort_keys=True)))


if __name__ == '__main__':
    exit(main())
