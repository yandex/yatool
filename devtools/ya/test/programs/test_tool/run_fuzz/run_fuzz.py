# coding: utf-8

from __future__ import print_function
import os
import io
import re
import sys
import six
import exts.yjson as json
import time
import shutil
import signal
import logging
import argparse
import threading
import traceback
import subprocess
import contextlib
import collections
import multiprocessing

import cityhash

import library.python.cores as cores

import devtools.ya.core.sec as sec
import exts.fs
import exts.archive
import yatest.common
from test import const
from test.util import tools, shared
from test.system import env as environment
from devtools.ya.test import facility
from test.const import Status
from test.test_types.common import PerformedTestSuite

logger = logging.getLogger(__name__)

MAX_FILE_SIZE = 1024 * 1024  # 1MiB
INTERRUPTION_EXITCODE = 78
SHUTDOWN_REQUESTED = threading.Event()
FUZZING_INTERRUPTED = [False]


def is_fuzzing_interrupted():
    return FUZZING_INTERRUPTED[0]


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, help="Path to the fuzz binary")
    parser.add_argument("--tracefile", help="Path to the output trace log")
    parser.add_argument("--output-dir", help="Path to the output dir")
    parser.add_argument("--project-path", help="Project path relative to arcadia")
    parser.add_argument("--modulo", default=1, type=int)
    parser.add_argument("--modulo-index", default=0, type=int)
    parser.add_argument(
        "--workers", dest="nworkers", help="Number of simultaneous worker processes to run the fuzzing jobs"
    )
    parser.add_argument(
        "--test-filter", default=[], action="append", help="Run only specified tests (binary name or mask)"
    )
    parser.add_argument("--fuzz-opts", default='', help="Space separated string of options")
    parser.add_argument("--output-corpus-dir")
    parser.add_argument("--source-root")
    parser.add_argument("--gdb-path", dest="gdb_path", action='store', default='')
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--list", action="store_true", help="List of tests")
    parser.add_argument("--truncate-logs", action="store_true", help="Truncate logs")
    parser.add_argument("--fuzzing", action="store_true")
    parser.add_argument("--fuzz-dict-path", default=[], action="append")
    parser.add_argument("--fuzz-case", help="Path to the certain baseunit")
    parser.add_argument("--fuzz-runs", help="Number of individual test runs", type=int, default=0)
    parser.add_argument("--fuzz-proof", help="Seconds for extra run since last found case", type=int, default=0)
    parser.add_argument("--dummy-run", action='store_true')
    parser.add_argument("--corpus-parts-limit-exceeded", type=int, default=0)

    args = parser.parse_args()
    args.binary = os.path.abspath(args.binary)
    if not os.path.exists(args.binary):
        parser.error("Binary doesn't exist: %s" % args.binary)
    if not args.list and not args.tracefile:
        parser.error("Path to the trace file must be specified")
    if not args.project_path:
        path = os.path.dirname(args.binary)
        if "arcadia" not in path:
            parser.error("Failed to determine project path")
        args.project_path = path.rsplit("arcadia")[1]
    args.project_path.strip("/")
    if not args.source_root:
        path = os.path.dirname(args.binary)
        if "arcadia" not in path:
            parser.error("Failed to determine source root")
        args.project_path = os.path.join(path.rsplit("arcadia")[0], "arcadia")
    if args.nworkers and args.nworkers == const.TestRequirementsConstants.All:
        args.nworkers = multiprocessing.cpu_count() - 1
    args.fuzzing = args.output_corpus_dir is not None
    if args.fuzz_case and not os.path.exists(args.fuzz_case):
        parser.error("Specified case doesn't exist: {}".format(args.fuzz_case))
    return args


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def readfile(filename):
    try:
        with io.open(filename, errors='ignore', encoding='utf-8') as afile:
            return afile.read()
    except Exception as e:
        return str(e)


def smooth_shutdown(signo, frame):
    SHUTDOWN_REQUESTED.set()


@contextlib.contextmanager
def watchdog_killer(proc, fuzzing):
    if not hasattr(signal, "SIGUSR2"):
        yield
        return

    def func():
        logger.debug("Watchdog for %d pid initialized", proc.pid)
        SHUTDOWN_REQUESTED.wait()
        alive = proc.poll() is None
        logger.debug("Event occurred for watchdog killer thread (proc %d is alive: %s)", proc.pid, alive)
        if alive:
            FUZZING_INTERRUPTED[0] = True
            # libFuzzer was forced to handle sigterm using -handle_term=1

            def term(pid):
                return os.killpg(proc.pid, signal.SIGTERM)

            def kill(pid):
                return os.killpg(proc.pid, signal.SIGKILL)

            # kill working processes, not main process - it doesn't set proper handler for SIGTERM in this case
            if fuzzing:
                for child_pid in yatest.common.process._nix_get_proc_children(proc.pid):
                    term(child_pid)
                    logger.debug("SIGTERM sent to fuzz worker %s", child_pid)
            else:
                term(proc.pid)
                logger.debug("SIGTERM sent to fuzz binary")

            holdup = 10
            while proc.poll() is None:
                time.sleep(1)
                holdup -= 1
                if not holdup:
                    logger.debug("Process is still alive. Status: %s", readfile("/proc/{}/stat".format(proc.pid)))
                    kill(proc.pid)
                    logger.debug("SIGKILL sent to fuzz binary")
                    break

    th = threading.Thread(target=func)
    th.daemon = True
    th.start()
    yield
    SHUTDOWN_REQUESTED.set()
    th.join()
    SHUTDOWN_REQUESTED.clear()


def purify_command(cmd):
    logger.debug("Dirty command: %s", cmd)
    command = collections.OrderedDict()
    for arg in cmd:
        if arg.startswith("-") and "=" in arg:
            key, val = arg.split("=", 1)
            command[key] = val
        else:
            command[arg] = None

    cmd = [k if v is None else "{}={}".format(k, v) for k, v in command.items()]
    return cmd


def gen_trace_record(name, data):
    return {
        'timestamp': time.time(),
        'name': name,
        'value': data,
    }


def open_test_stage(suite, tracefile, logs):
    assert len(suite.chunk.tests) == 1
    test_case = suite.chunk.tests[0]
    with open(tracefile, "a") as afile:
        json.dump(
            gen_trace_record(
                "subtest-started",
                {
                    "class": test_case.get_class_name(),
                    "subtest": test_case.get_test_case_name(),
                    "logs": logs,
                },
            ),
            afile,
        )
        afile.write("\n")


def suggest_workers_count(workers, opts):
    mem = shared.get_available_memory_in_mb()
    rss_limit_mb = 2048  # default libfuzzer value
    for opt in opts:
        if opt.startswith("-rss_limit_mb="):
            rss_limit_mb = int(opt.split("=", 1)[1])
    # binary may consume memory with chunks and rss limit detector will be triggered with delay
    # to prevent the exhaustion of memory we increase specified limit
    confident_rss_limit_mb = int(rss_limit_mb * 1.2)
    free_buff_size_mb = min(4096, int(mem * 0.2))
    suggestion = (mem - free_buff_size_mb) // confident_rss_limit_mb
    suggestion = max(1, min(workers, suggestion))
    logger.debug(
        "Suggested number of workers: %d (available mem: %dMb, selected free buff: %dMb, requested rss_limit_mb: %dMb, calculated rss_limit_mb: %dMb)",
        suggestion,
        mem,
        free_buff_size_mb,
        rss_limit_mb,
        confident_rss_limit_mb,
    )
    return suggestion


def execute_fuzz(cmd, outfile, errfile, fuzzing_mode):
    cmd = purify_command(cmd)
    logger.info("Command: '%s'", format(" ".join(cmd)))

    with open(outfile, "a") as stdout, open(errfile, "a") as stderr:
        for s in [stdout, stderr]:
            s.write("Command: {}\n".format(cmd))

        proc = subprocess.Popen(cmd, stdout=stdout, stderr=stderr, preexec_fn=os.setpgrp)
        with watchdog_killer(proc, fuzzing_mode):
            proc.wait()
    logger.debug("Return code: %d", proc.returncode)
    return proc


def build_cmd(binary, output_corpus_dir, corpus_dirs, user_opts, nruns, nworkers, dict_path, fuzzing_mode):
    # options that might be overwritten by user
    defaults = [
        "-max_total_time=600",
        "-rss_limit_mb=4096",
        "-timeout=600",
    ]
    # options that might not be redefined
    adjustment_opts = [
        output_corpus_dir,
        "-print_final_stats=1",
        "-handle_term=1",
        "-interrupted_exitcode={}".format(INTERRUPTION_EXITCODE),
        "-dump_interrupted=1",
        "-artifact_prefix={}/".format(output_corpus_dir),
        # force to call fuzzer __sanitizer_dump_coverage to generate *.sancov files to get proper metrics
        "-dump_coverage=1",
        # the default lower bound for the notification is 10s - drop it
        # https://a.yandex-team.ru/arc/trunk/arcadia/contrib/libs/libfuzzer7/FuzzerOptions.h?rev=5370656#L43
        "-report_slow_units=0",
    ]

    cmd = [binary] + defaults + user_opts + adjustment_opts

    if fuzzing_mode:
        if nworkers:
            nworkers = suggest_workers_count(nworkers, cmd)
            cmd += ["-jobs={}".format(nworkers), "-workers={}".format(nworkers)]

            if nworkers > 1 and nruns:
                nruns = (nruns / nworkers) + 1

        if nruns:
            cmd.append("-runs={}".format(nruns))
        else:
            cmd.append("-runs=-1")

        if dict_path:
            cmd += ["-dict={}".format(dict_path)]
    else:
        cmd.append("-runs=0")

    return cmd + corpus_dirs


def get_secs_since_last_found_case(path, started):
    curr = int(time.time())

    files = [os.path.join(path, f) for f in os.listdir(path)]

    if files:
        m = {}
        for filename in files:
            m[filename] = os.stat(filename).st_mtime
        last = sorted(m.items(), key=lambda x: x[1])[-1][1]
        return curr - last
    else:
        return curr - started


class PollThread(object):
    def __init__(self, target, args, delay=1):
        self.target = target
        self.args = args
        self.delay = delay
        self.thread = None
        self.event = threading.Event()

    def start(self):
        assert not self.thread
        self.thread = threading.Thread(target=self.run)
        self.thread.daemon = True
        self.thread.start()

    def run(self):
        try:
            while not self.event.is_set():
                stop = bool(self.target(*self.args))
                if stop:
                    return
                self.event.wait(self.delay)
        except Exception as e:
            logger.exception("Error in PollThread({}, {}): {}".format(self.target, self.args, e))

    def __enter__(self):
        self.start()

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.join()

    def join(self):
        assert self.thread
        self.stop()
        self.thread.join()
        self.thread = None

    def stop(self):
        self.event.set()


def launch_fuzz(suite, corpus_dirs, logsdir, params, fuzz_options=None, fuzz_dicts=None):
    fuzz_options = fuzz_options or []
    # Don't collect core dump files if truncation is required
    collect_cores = not params.truncate_logs
    outfile = os.path.join(logsdir, "fuzz.out")
    errfile = os.path.join(logsdir, "fuzz.err")

    logs = {
        'logsdir': logsdir,
        'stderr': errfile,
        'stdout': outfile,
    }

    output_corpus_dir = params.output_corpus_dir
    if not output_corpus_dir:
        # if there was crash during corpus run, store input which led to crash in testing_output_stuff
        output_corpus_dir = os.path.join(logsdir, "corpus")
        exts.fs.ensure_dir(output_corpus_dir)

    # We need to make a record in the tracefile, that we've started test.
    # Thus, in case of timeout, run_test will found that record and set timeout status to the test.
    # We don't need to close stage - it'll be overwritten later (with dump_trace_file)
    open_test_stage(suite, params.tracefile, logs)

    # TODO remove after DEVTOOLS-3216
    for dirname in corpus_dirs:
        files = os.listdir(dirname)
        logger.debug("%d files in %s (%s)", len(files), dirname, sorted(files)[:5])

    cmd = build_cmd(
        params.binary,
        output_corpus_dir,
        corpus_dirs,
        fuzz_options,
        params.fuzz_runs,
        params.nworkers,
        get_fuzz_dict_path(logsdir, fuzz_dicts) if fuzz_dicts else None,
        params.fuzzing,
    )

    started = time.time()
    proc = execute_fuzz(cmd, outfile, errfile, params.fuzzing)
    fuzz_duration = time.time() - started
    fuzz_proof_duration = 0
    fuzz_proof_cases = []

    if params.fuzz_proof and proc.returncode == 0 and params.fuzzing:
        time_passed = get_secs_since_last_found_case(output_corpus_dir, started)
        time_left = max(int(params.fuzz_proof - time_passed), 0)
        logger.debug(
            "Requested fuzz proof: %ds, time passed since last case: %ds, extra fuzz time is %ds",
            params.fuzz_proof,
            time_passed,
            time_left,
        )
        params.fuzz_proof = time_left

        extra_output = os.path.join(output_corpus_dir, "..", "fuzz_proof_output")
        exts.fs.ensure_dir(extra_output)

        cmd = build_cmd(
            params.binary,
            extra_output,
            corpus_dirs + [output_corpus_dir],
            fuzz_options
            + [
                "-max_total_time={}".format(params.fuzz_proof),
                # libFuzzer might save reduced case (with smaller size) in output_corpus_dir,
                # that would break fuzz proof falsely
                "-reduce_inputs=0",
            ],
            0,
            params.nworkers,
            get_fuzz_dict_path(logsdir, fuzz_dicts) if fuzz_dicts else None,
            params.fuzzing,
        )

        persistent_proof = os.environ.get('YA_TEST_FUZZ_PERSISTENT_PROOF') in ('yes', '1')
        logger.debug("Persistent proof mode is %s", "enabled" if persistent_proof else "disabled")

        def search_new_cases(path):
            fuzz_proof_cases[:] = os.listdir(path)
            if fuzz_proof_cases and not persistent_proof:
                logger.debug("Fuzz proof failed - found new case")
                SHUTDOWN_REQUESTED.set()
                return True

        if params.fuzz_proof:
            fuzz_proof_started = time.time()
            with PollThread(target=search_new_cases, args=(extra_output,)):
                # Overwrite proc object to run standart machinery for fuzz-proof run in case of error
                proc = execute_fuzz(cmd, outfile, errfile, params.fuzzing)
            fuzz_proof_duration = time.time() - fuzz_proof_started

        # Store all new cases except interrupted
        fuzz_proof_cases = [f for f in os.listdir(extra_output) if not f.startswith("interrupted-")]
        logger.debug("Found %d new case(s): %s", len(fuzz_proof_cases), fuzz_proof_cases)
        for f in fuzz_proof_cases:
            shutil.copyfile(os.path.join(extra_output, f), os.path.join(output_corpus_dir, f))

    if params.truncate_logs:
        logger.debug("Truncating logs to %d bytes", MAX_FILE_SIZE)
        tools.truncate_logs([outfile, errfile], MAX_FILE_SIZE)

    # We have no time for coredump processing in case of interruption
    if proc.returncode < 0 and not is_fuzzing_interrupted():
        logger.debug("Trying to recover dump core file")
        filename = os.path.basename(cmd[0])
        shared.postprocess_coredump(
            params.binary, os.getcwd(), proc.pid, logs, params.gdb_path, collect_cores, filename, logsdir
        )

    # try to recover input, which led to failure
    files = sorted(os.listdir(output_corpus_dir))
    slow_units, files = split_slow_units(files)
    if files:
        baseunit = None
        if params.fuzzing:
            cases = collections.defaultdict(list)
            for filename in files:
                if "-" not in filename:
                    continue
                type_name = filename.split("-")[0]
                cases[type_name].append(filename)

            logger.warning("Found base unit cases: %s", cases)
            if cases:
                priority = {
                    "crash": 2,
                    "interrupted": 1,
                }
                # take most valuable [-1] filename [1] from list of files [0]
                logger.debug("Found %d cases", len(cases))
                baseunit = sorted(six.iteritems(cases), key=lambda x: priority.get(x[0], 0))[-1][1][0]
        else:
            if len(files) > 1:
                logger.warning("Found more than one base unit (impossibru): %s", files[:5])
            baseunit = files[0]
        if baseunit:
            filename = os.path.join(logsdir, baseunit)
            logger.debug("Using %s as baseunit", baseunit)
            shutil.copyfile(os.path.join(output_corpus_dir, baseunit), os.path.join(logsdir, baseunit))
            logs['baseunit'] = filename

    if slow_units:
        with io.open(errfile, errors='ignore', encoding='utf-8') as afile:
            data = afile.read()
        logger.debug("Searching for slow units")
        m = re.search(r".*Slowest unit.*?Test unit written to (.*?/(slow-unit-.*?))\n", data, flags=re.DOTALL)
        if m:
            filename = m.group(1)
            baseunit = m.group(2)
            slow_unit_filename = os.path.join(logsdir, baseunit)
            logger.debug("Storing %s -> %s", filename, slow_unit_filename)
            shutil.copyfile(filename, slow_unit_filename)
            logs['slowest_baseunit'] = slow_unit_filename
        else:
            logging.warning("Failed to find slowest baseunit")

    stats = {}
    if params.fuzzing and os.path.exists(output_corpus_dir):
        remove_corpus_data_duplicates(output_corpus_dir, corpus_dirs)

        stats["mined_corpus_size"] = len(os.listdir(output_corpus_dir))

    if fuzz_proof_duration:
        stats["fuzz_proof_extra_secs"] = fuzz_proof_duration

    test_case = suite.chunk.tests[0]
    test_case.logs = logs
    test_case.elapsed = fuzz_duration
    test_case.status = Status.GOOD
    test_case.comment = ""
    test_case.metrics = get_metrics(errfile, stats, corpus_dirs)

    if fuzz_proof_cases:
        test_case.status = Status.FAIL
        test_case.comment = (
            "[[bad]]Fuzz proof failed after [[imp]]{:0.2f}s[[bad]] - found [[imp]]{}[[bad]] new cases".format(
                fuzz_proof_duration,
                len(fuzz_proof_cases),
            )
        )
    elif proc.returncode:
        set_test_case_status(test_case, errfile, proc.returncode, params.fuzzing)

    if 'backtrace' in test_case.logs:
        with io.open(test_case.logs['backtrace'], errors='ignore', encoding='utf-8') as afile:
            backtrace = cores.get_problem_stack(afile.read())
            backtrace = cores.colorize_backtrace(backtrace)
            test_case.comment += "\n[[rst]]Problem thread backtrace:\n{}[[rst]]".format(backtrace)

    suite.chunk.logs = {'logsdir': logsdir}
    shared.dump_trace_file(suite, params.tracefile)
    return proc.returncode


def get_fuzz_dict_path(logsdir, fuzz_dicts):
    if len(fuzz_dicts) == 1:
        return fuzz_dicts[0]

    dict_path = os.path.join(logsdir, "fuzz_merged_dict.txt")
    units = collections.OrderedDict()
    for filename in fuzz_dicts:
        with open(filename) as afile:
            for line in afile:
                line = line.strip(" \n\t")
                if line and line not in units:
                    units[line] = 1

    with open(dict_path, "w") as afile:
        for unit in units.keys():
            afile.write("{}\n".format(unit))
    return dict_path


def set_test_case_status(test_case, errfile, rc, fuzzing):
    if is_fuzzing_interrupted():
        test_case.status = Status.TIMEOUT
        test_case.comment = (
            "[[bad]]Fuzzy test was interrupted by timeout.\n"
            "It may occur in several situations:\n"
            " * fuzzer has found some amount of interesting data - may be you just need to increase test size (small -> medium, medium -> large)\n"
            " * fuzzer has found a really slow case and you should check it out\n"
            " * your target function works really slow - you should check fuzz_iterations_per_second metric, profile code and increase test size\n"
            " * fuzzer found too much cases - probably you should increase test size\n\n"
        )
        with io.open(errfile, errors='ignore', encoding='utf-8') as afile:
            test_case.comment += afile.read()
        return

    error_msg = extract_found_errors(errfile)
    if error_msg:
        test_case.status = Status.FAIL
        test_case.comment = "[[bad]]Test crashed (return code: [[imp]]{}[[bad]])\n{}".format(
            rc, shared.colorize_sanitize_errors(error_msg)
        )
        return

    error_msg = shared.get_sanitizer_first_error(errfile)
    if error_msg:
        test_case.status = Status.FAIL
        test_case.comment = "[[bad]]Test crashed (return code: [[imp]]{}[[bad]])\n{}".format(
            rc, shared.colorize_sanitize_errors(error_msg)
        )
        return

    error_msg = search_for_misconfiguration(errfile)
    if error_msg:
        if fuzzing:
            test_case.status = Status.SKIPPED
            test_case.comment = error_msg
        # If no errors found - it looks like libfuzzer is just capricious about missing coverage,
        # that's why rc != 0, but we aren't fuzzing - so it's ok
        return

    test_case.status = Status.FAIL
    with io.open(errfile, errors='ignore', encoding='utf-8') as afile:
        test_case.comment = "[[bad]]Test crashed (return code: [[imp]]{}[[bad]])\n{}".format(rc, afile.read())


def search_for_misconfiguration(filename):
    with io.open(filename, errors='ignore', encoding='utf-8') as afile:
        for line in afile:
            for pattern, suggest in [
                (
                    "ERROR: __sanitizer_set_death_callback is not defined",
                    "Did you use --sanitize=... to build your code?",
                ),
                (
                    "ERROR: no interesting inputs were found. Is the code instrumented for coverage?",
                    "Did you use {} to build your code?".format(' '.join(const.FUZZING_COVERAGE_ARGS)),
                ),
            ]:
                if pattern in line:
                    return "Wrong build configuration - {}\n{}".format(line, suggest)


def extract_found_errors(filename):
    err_regexps = [re.compile(p) for p in (r'=+\d+=+\s*ERROR', r'=+\d+=+\s*WARNING')]
    stack_end_regexp = re.compile(r'=+\s*Job \d+ exited')
    found = False
    data = []

    with io.open(filename, errors='ignore', encoding='utf-8') as afile:
        while True:
            line = afile.readline()
            if not line:
                break

            if not found:
                for regexp in err_regexps:
                    if regexp.match(line):
                        found = True
                        break
            if found:
                if stack_end_regexp.search(line):
                    return "".join(data)
                data.append(line)
    if data:
        return "".join(data)


def remove_corpus_data_duplicates(target, dirs):
    logger.debug("Searching for duplicates in: %s", target)
    found = {}
    for dirname in dirs:
        for root, dirs, files in os.walk(dirname):
            for filename in files:
                filename = os.path.join(root, filename)
                found[cityhash.filehash64(six.ensure_binary(filename))] = filename

    for root, dirs, files in os.walk(target):
        for filename in files:
            filename = os.path.join(root, filename)
            hashval = cityhash.filehash64(six.ensure_binary(filename))
            if hashval in found:
                logger.debug("Removing '%s' as duplicate of '%s'", filename, found[hashval])
                exts.fs.ensure_removed(filename)


def get_metrics(fuzz_output, stats, corpus_dirs):
    with io.open(fuzz_output, errors='ignore', encoding='utf-8') as afile:
        fuzz_data = afile.read()

    corpus_size = 0
    for dirname in corpus_dirs:
        nfiles = sum(len(files) for r, d, files in os.walk(dirname))
        logger.debug("Corpus consist of %d files in %s", nfiles, dirname)
        corpus_size += nfiles

    metrics = {
        "corpus_size": corpus_size,
    }
    metrics.update(stats)

    # every workers dumps own stats - viewed them all
    def summarize(x, y):
        return x + y

    for name, regex, func in [
        ("covered_edges", re.compile(r"#\d+\s*DONE\s+cov:\s*(\d+)"), max),
        ("peak_rss_mb", re.compile(r"stat::peak_rss_mb:\s*(\d+)"), max),
        ("fuzz_iterations_per_second", re.compile(r"stat::average_exec_per_sec:\s*(\d+)"), summarize),
        ("number_of_executed_units", re.compile(r"stat::number_of_executed_units:\s*(\d+)"), summarize),
        ("slowest_unit_time_sec", re.compile(r"stat::slowest_unit_time_sec:\s*(\d+)"), max),
    ]:
        for match in regex.finditer(fuzz_data):
            metrics[name] = func(metrics.get(name, 0), int(match.group(1)))

    return metrics


def get_corpus_dirs(source_root, project_path, fuzz_case, fuzzing, modulo, modulo_index):
    if fuzz_case:
        dirname = "fuzz_case"
        exts.fs.ensure_dir(dirname)
        exts.fs.symlink(fuzz_case, os.path.join(dirname, os.path.basename(fuzz_case)))
        return [os.path.abspath(dirname)]

    corpus_dirs = []
    local_corpus = os.path.join(source_root, project_path, const.CORPUS_DIR_NAME)
    if os.path.exists(local_corpus):
        corpus_dirs.append(local_corpus)

    extra_corpus_dir = os.environ.get('YA_TEST_FUZZ_EXTRA_CORPUS_PATH')
    if extra_corpus_dir and os.path.exists(extra_corpus_dir):
        corpus_dirs.append(extra_corpus_dir)

    logger.debug("Looking for dirs with automatically generated data for fuzzing")
    corpus_parts_dir = "corpus_parts"
    if os.path.exists(corpus_parts_dir):
        for n in sorted(os.listdir(corpus_parts_dir)):
            datadir = os.path.abspath(os.path.join(corpus_parts_dir, n))
            files = [f for f in os.listdir(datadir) if f.endswith((".tar", ".tar.gz"))]
            if not files:
                logger.error("Corpus data dir (%s) doesn't contain archive: %s", datadir, os.listdir(datadir))
                continue
            elif len(files) != 1:
                logger.warning("Corpus data dir (%s) doesn't contain single archive: %s", datadir, files)
                continue

            filename = os.path.join(datadir, files[0])
            exts.archive.extract_from_tar(filename, datadir)
            exts.fs.ensure_removed(filename)
            corpus_dirs.append(datadir)

    if not fuzzing and modulo > 1:
        cases = {}
        for dirname in corpus_dirs:
            for root, dirs, files in os.walk(dirname):
                basename = os.path.basename(root)
                for filename in files:
                    key = "{}_{}".format(basename, filename)
                    assert key not in cases, key
                    cases[key] = os.path.join(root, filename)

        logger.debug("Corpus size: %d", len(cases))
        shift = (len(cases) + modulo - 1) // modulo
        start = modulo_index * shift
        end = start + shift
        logger.debug("%d chunk corpus size: %d", modulo_index, start - end)

        dirname = os.path.abspath("corpus_chunk")
        exts.fs.ensure_dir(dirname)
        for dst, src in sorted(six.iteritems(cases))[start:end]:
            exts.fs.symlink(src, os.path.join(dirname, dst))
        corpus_dirs = [dirname]

    return corpus_dirs


def gen_suite():
    suite = PerformedTestSuite()
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def setup_env():
    for san in ["LSAN", "ASAN", "UBSAN", "MSAN"]:
        san_opts = "{}_OPTIONS".format(san)
        for param in [
            # enable sancov
            "coverage=1",
            # https://st.yandex-team.ru/DEVTOOLS-4006
            "allocator_may_return_null=1",
        ]:
            os.environ[san_opts] = environment.extend_env_var(os.environ, san_opts, param)

    if hasattr(signal, "SIGUSR2"):
        signal.signal(signal.SIGUSR2, smooth_shutdown)


def split_slow_units(files):
    slow, rest = [], []
    for f in files:
        if f.startswith('slow-unit-'):
            slow.append(f)
        else:
            rest.append(f)
    return slow, rest


def main():
    args = parse_args()

    if args.list:
        print("fuzz::test")
        return 0

    setup_logging(logging.DEBUG if args.fuzzing else logging.INFO)
    setup_env()
    logger.debug("Environment variables: %s", json.dumps(sec.environ(), sort_keys=True, indent=4))

    if args.tracefile:
        open(args.tracefile, "w").close()

    suite = gen_suite()
    chunk = suite.chunk
    try:
        test_case = facility.TestCase("fuzz::test", Status.NOT_LAUNCHED, "Test was not launched")
        chunk.tests.append(test_case)
        shared.dump_trace_file(suite, args.tracefile)

        if args.output_corpus_dir:
            os.mkdir(args.output_corpus_dir)

        if args.dummy_run:
            return 1

        logsdir = args.output_dir or os.path.dirname(args.tracefile) or os.getcwd()
        chunk.logs = {"logsdir": logsdir}

        corpus_dirs = get_corpus_dirs(
            args.source_root, args.project_path, args.fuzz_case, args.fuzzing, args.modulo, args.modulo_index
        )

        if args.corpus_parts_limit_exceeded:
            fuzz_project_path = os.path.join(const.CORPUS_DATA_ROOT_DIR, args.project_path)
            comment = (
                "[[bad]]Project [[imp]]{fuzz_project}[[bad]] has [[imp]]{nparts}[[bad]] corpus parts, while the limit "
                "on the number of parts before minimization involved is [[imp]]{limit}[[bad]].\n"
                "This might happen when you have enormous amount of cases in corpus or your LLVMFuzzerTestOneInput works really slow and "
                "minimization takes to much time and get killed by machinery. Looks like minimization can't be done without your intervention.\n"
                "Firstly, you should check your test's history in TestEnv and read some logs.\n"
                "Secondly, try to reproduce the problem. Fuzzer could found some extra slow sequent of cases you might be interested in\n"
                "Thirdly, you can drop some corpus parts from {fuzz_project}/corpus.json and reduce frequency of adding new data to the corpus (if you really add it)\n"
                "Fourthly, you can perform minimization on your dev-host using --fuzz-minimization-only key\n"
                "If you've questions - write on devtools@".format(
                    nparts=args.corpus_parts_limit_exceeded,
                    limit=const.MAX_CORPUS_RESOURCES_ALLOWED,
                    fuzz_project=fuzz_project_path,
                )
            )
            chunk.add_error(comment, Status.FAIL)
            shared.dump_trace_file(suite, args.tracefile)

            sys.stderr.write(comment + "\n")
            return 1

        fuzz_dicts = []
        for filename in args.fuzz_dict_path:
            if os.path.exists(filename):
                fuzz_dicts.append(filename)
            else:
                logger.debug("Specified dict doesn't exist: %s", filename)

        fuzz_opts = [_f for _f in args.fuzz_opts.split(" ") if _f]
        return launch_fuzz(
            suite=suite,
            corpus_dirs=corpus_dirs,
            logsdir=logsdir,
            params=args,
            fuzz_options=fuzz_opts,
            fuzz_dicts=fuzz_dicts,
        )
    except Exception:
        comment = "[[bad]]Internal error: {}".format(traceback.format_exc())
        chunk.add_error(comment, Status.INTERNAL)
        shared.dump_trace_file(suite, args.tracefile)
        raise


if __name__ == "__main__":
    exit(main())
