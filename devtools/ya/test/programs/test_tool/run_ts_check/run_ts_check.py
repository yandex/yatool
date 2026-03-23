import json
import logging
import os
import shutil
import stat
import time

import build.plugins.lib.nots.package_manager.constants as pm_const
import build.plugins.lib.nots.package_manager.utils as pm_utils
from devtools.ya.test.const import Status
from devtools.ya.test.facility import TestCase
from devtools.ya.test.system.process import execute
from devtools.ya.test.test_types.common import PerformedTestSuite
from devtools.ya.test.util.shared import setup_logging

from .cli_args import parse_args, CliArgs
from .colors import simplify_colors

logger = logging.getLogger("run_ts_check")

STATUS_MAPPING = {
    "OK": Status.GOOD,
    "FAILED": Status.FAIL,
    "SKIPPED": Status.SKIPPED,
    "NOT_LAUNCHED": Status.NOT_LAUNCHED,
}


def main():
    args = parse_args()
    # logging.INFO is a level of stderr stream
    # for args.log_path stream level is logging.DEBUG
    setup_logging(logging.INFO, args.log_path)
    return run(args)


def get_env(args: CliArgs, report_path: str):
    build_dir = os.path.join(args.build_root, args.target_path)
    bindir_node_modules_path = os.path.join(build_dir, pm_const.NODE_MODULES_DIRNAME)
    node_path = [
        bindir_node_modules_path,
        os.path.join(pm_utils.build_vs_store_path(args.build_root, args.target_path), pm_const.NODE_MODULES_DIRNAME),
        # TODO: remove - no longer needed
        os.path.join(bindir_node_modules_path, pm_const.VIRTUAL_STORE_DIRNAME, pm_const.NODE_MODULES_DIRNAME),
        os.path.join(build_dir, pm_const.VIRTUAL_STORE_DIRNAME, pm_const.NODE_MODULES_DIRNAME),
    ]

    return {
        "PATH": args.nodejs,
        "NODE_PATH": os.pathsep.join(node_path),
        "NODE_OPTIONS": "--max-old-space-size=4096",
        "YA_TEST_REPORT_PATH": report_path,
    }


def copy_files(src_dir: str, build_dir: str, files: list[str]):
    created_dirs = set()

    def makedirs(path: str):
        if path not in created_dirs:
            os.makedirs(path, exist_ok=True)
            created_dirs.add(path)

    for file in files:
        src_file = os.path.join(src_dir, file)
        dst_file = os.path.join(build_dir, file)
        if not os.path.exists(dst_file):
            makedirs(os.path.dirname(dst_file))
            shutil.copyfile(src_file, dst_file, follow_symlinks=False)
            os.chmod(dst_file, os.stat(dst_file).st_mode | stat.S_IWRITE)


def create_suite(cwd: str, log_path: str) -> PerformedTestSuite:
    suite = PerformedTestSuite(None, None, None)
    suite.set_work_dir(cwd)
    suite.register_chunk()
    suite.chunk.logs[os.path.basename(log_path)] = log_path
    return suite


def parse_event(line: str, script_name: str) -> TestCase:
    """Parse a single event line from the report file and return a TestCase."""
    event = json.loads(line)
    name = event.get("name")
    subtest_name = event.get("subtestName")
    subtest_name = f"{script_name}/{subtest_name}" if subtest_name else script_name

    status = STATUS_MAPPING.get(event.get("status"))
    duration = event.get("duration", 0)
    test_case = TestCase(
        name=f"{name}::{subtest_name}",
        status=status,
        comment=simplify_colors(event.get("richSnippet")) if status == Status.FAIL else "",
        elapsed=duration / 1000,
        metrics=event.get("metrics"),
        tags=event.get("tags"),
    )
    return test_case


class ReportFileWatcher:
    """Process progress listener that watches report file and processes new lines."""

    def __init__(self, report_path: str, suite: PerformedTestSuite, script_name: str, tracefile: str):
        self.report_path = report_path
        self.suite = suite
        self.script_name = script_name
        self.tracefile = tracefile
        self.last_position = 0
        self.last_check_time = 0
        self.check_interval = 0.2  # Check every 500ms

    def open(self, command, process, out_file, err_file):
        """Called when process starts."""
        pass

    def __call__(self):
        """Called periodically while process is running."""
        current_time = time.monotonic()
        # Throttle checks to avoid excessive file I/O
        if current_time - self.last_check_time < self.check_interval:
            return

        self.last_check_time = current_time
        self._check_report_file()

    def _check_report_file(self):
        """Read new content from report file and process it."""
        if os.path.exists(self.report_path):
            try:
                with open(self.report_path, 'r') as f:
                    f.seek(self.last_position)
                    new_content = f.read()
                    if new_content:
                        self.last_position = f.tell()
                        lines = new_content.splitlines()
                        self._process_event_lines(lines)
            except (IOError, OSError) as e:
                logger.debug(f"Error reading report file: {e}")

    def _process_event_lines(self, lines: list[str]):
        """Process event lines from report file."""
        for line in lines:
            test_case = parse_event(line, self.script_name)
            self.suite.chunk.tests.append(test_case)
        self.suite.generate_trace_file(self.tracefile, append=True)

    def close(self):
        """Called when process finishes - do final check."""
        self._check_report_file()


def run(args: CliArgs):
    logger.debug(f"{args.source_root=}")
    logger.debug(f"{args.build_root=}")
    logger.debug(f"{args.target_path=}")

    report_path = os.path.join(args.output_dir, "report.jsonl")
    node_run_log = os.path.join(args.output_dir, "node-run.log")
    src_dir = os.path.join(args.source_root, args.target_path)
    build_dir = os.path.join(args.build_root, args.target_path)
    cwd = build_dir

    copy_files(src_dir, build_dir, args.files)
    cmd = get_cmd(args)
    suite = create_suite(cwd, args.log_path)

    # Create progress listener that will watch the report file
    watcher = ReportFileWatcher(report_path, suite, args.script_name, args.tracefile)

    start_time = time.monotonic()
    res = execute(
        cmd,
        cwd=cwd,
        env=get_env(args, report_path),
        check_exit_code=False,
        stderr=node_run_log,
        stdout_to_stderr=True,
        timeout=10000000,  # without timeout process_progress_listener is not called periodically
        process_progress_listener=watcher,
    )
    messages = []
    if res.exit_code != 0:
        messages = [
            " ".join(cmd),
            f"Exit code: {res.exit_code}",
            "output:",
            res.stderr,
        ]

    test_case = TestCase(
        name=f"{args.test_type}::node-run",
        status=Status.FAIL if res.exit_code != 0 else Status.GOOD,
        comment=simplify_colors("\n".join(messages)),
        elapsed=time.monotonic() - start_time,
        logs={os.path.basename(node_run_log): node_run_log},
    )
    suite.chunk.tests.append(test_case)

    logger.debug(f"Generate trace file '{args.tracefile}'")
    logger.debug(f"Found tests count: {len(suite.chunk.tests)}")
    suite.generate_trace_file(args.tracefile, append=True)
    return 0


def get_cmd(args: CliArgs):
    cmd = [
        "node",
        "--run",
        args.script_name,
    ]
    return cmd
