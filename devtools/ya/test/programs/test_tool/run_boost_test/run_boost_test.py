# coding: utf-8

from __future__ import print_function
import argparse
import logging
import os
import signal
import subprocess
import sys

import xml.dom.minidom as xml_parser
from xml.parsers.expat import ExpatError

import yalibrary.display
import yalibrary.formatter

from test.system import process
from test.common import get_test_log_file_path, strings_to_utf8
from devtools.ya.test import facility
from test.const import Status
from test.test_types.common import PerformedTestSuite
from test.util import shared

import test.filter as test_filter
import test.ios.simctl_control as ios_simctl_control
import test.android.android_emulator as android_control
import exts.uniq_id

from yatest.common import cores

logger = logging.getLogger(__name__)

MASTER_TEST_SUITE = "Master Test Suite"

ANDROID_MAX_RETRY_COUNT = 5


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--binary", required=True, help="Path to the unittest binary")
    parser.add_argument("-t", "--trace-path", help="Path to the output trace log")
    parser.add_argument("-o", "--output-dir", help="Path to the output dir")
    parser.add_argument(
        "-f", "--test-filter", default=[], action="append", help="Run only specified tests (binary name or mask)"
    )
    parser.add_argument("-p", "--project-path", help="Project path relative to arcadia")
    parser.add_argument("--tracefile", help="Path to the output trace log")
    parser.add_argument("--test-list", action="store_true", help="List of tests")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--test-mode", action="store_true")
    parser.add_argument("--gdb-debug", action="store_true")
    parser.add_argument("--gdb-path", help="Path to gdb")
    parser.add_argument("--ios-app", action="store_true", help="Test binary is iOS app")
    parser.add_argument("--ios-simctl", default=None, help="Path to Xcode simctl binary (need for iOS app)")
    parser.add_argument("--ios-profiles", default=None, help="Path to Xcode simulators profiles (need for iOS app)")
    parser.add_argument("--ios-device-type", default=None, help="iPhone simulator device type (need for iOS app)")
    parser.add_argument("--ios-runtime", default=None, help="iPhone simulator runtime (need for iOS app)")
    parser.add_argument("--android-app", action="store_true", help="Test binary is android app")
    parser.add_argument("--android-sdk", default=None, help="Path to android sdk root (need for android apk)")
    parser.add_argument(
        "--android-avd", default=None, help="Path to directory with template avd's (need for android apk)"
    )
    parser.add_argument("--android-arch", default=None, help="Android apk architecture")
    parser.add_argument("--android-activity", default=None, help="Android activity name")

    args = parser.parse_args()
    args.binary = os.path.abspath(args.binary)
    if not os.path.exists(args.binary):
        parser.error("Test binary doesn't exist: %s" % args.binary)
    if not args.test_list and not args.tracefile:
        parser.error("Path to the trace file must be specified")
    return args


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def get_tests(args):
    if args.ios_app:
        device_name = "-".join([_f for _f in ["rnd", exts.uniq_id.gen16()] if _f])
        ios_simctl_control.prepare(
            args.ios_simctl,
            args.ios_profiles,
            os.getcwd(),
            device_name,
            args.binary,
            args.ios_device_type,
            args.ios_runtime,
        )
        res = ios_simctl_control.run(args.ios_simctl, args.ios_profiles, os.getcwd(), device_name, ["list"])
        ios_simctl_control.cleanup(args.ios_simctl, args.ios_profiles, os.getcwd(), device_name)
    elif args.android_app:
        device_name = "-".join([_f for _f in ["rnd", exts.uniq_id.gen16()] if _f])
        if args.android_arch not in ('i686', 'x86_64'):
            raise Exception('Only i686 and x86_64 architectures supported for tests run')
        arch = 'x86' if args.android_arch == 'i686' else 'x86_64'
        entry_point = args.android_activity
        app_name = entry_point.split('/')[0]
        end_marker = '/data/data/{}/ya.end'.format(app_name)
        with android_control.AndroidEmulator(os.getcwd(), args.android_sdk, args.android_avd, arch) as android:
            logger.info("AVD name is {}".format(device_name))
            logger.info("Boot AVD")
            android.boot_device(device_name)
            logger.info("Install test apk")
            android.install_app(device_name, args.binary, app_name)
            logger.info("Run test apk with list")
            res = android.run_list(
                device_name, entry_point, app_name, end_marker, ['list', '--ya-report={}'.format(end_marker)]
            )
    else:
        res = process.execute([args.binary, "list"])
    tests = []
    for line in [_f for _f in res.std_out.split("\n") if _f]:
        # skip app bundle name (always printed by simctl)
        if line.startswith('Yandex.devtools_ios_wraper:'):
            continue
        test_name, subtest_name = line.split("::", 1)
        if test_name == MASTER_TEST_SUITE:
            test_name = get_default_suite_name(args.binary)
        tests.append("{}::{}".format(test_name, subtest_name))
    if args.test_filter:
        tests = list(filter(test_filter.make_testname_filter(args.test_filter), tests))
    return tests


def gen_suite(project_path):
    suite = PerformedTestSuite(None, project_path)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()
    return suite


def get_default_suite_name(binary):
    return os.path.splitext(os.path.basename(binary))[0]


def load_tests_from_log(opts, suite, boost_log_path):
    with open(boost_log_path) as log:
        xml = log.read()
        if xml:
            # XXX CDATA can't store binary data - convert escape codes to the markup
            xml = yalibrary.formatter.ansi_codes_to_markup(xml)
            try:
                report = xml_parser.parseString(xml)
            except ExpatError as e:
                logger.error('Boost log has invalid xml\n%s', e)
                return
        else:
            return

    def get_test_name(test_case_el):
        subtest_name = test_case_el.attributes["name"].value
        parent_el = test_case_el.parentNode
        parents = []

        # boost test bug workaround https://st.yandex-team.ru/MAPSMOBCORE-20420
        while not isinstance(parent_el, xml_parser.Document) and parent_el.tagName != "TestLog":
            if parent_el.attributes["name"].value == MASTER_TEST_SUITE:
                parent = get_default_suite_name(opts.binary)
            else:
                parent = parent_el.attributes["name"].value
            parents.insert(0, parent)
            parent_el = parent_el.parentNode
        return "{}::{}".format("::".join(parents), subtest_name)

    for test_case_el in report.getElementsByTagName("TestCase"):
        for test_time_el in test_case_el.getElementsByTagName("TestingTime"):
            elapsed = int(test_time_el.childNodes[0].wholeText) / 1e6  # reported in microseconds
            break
        else:
            elapsed = 0

        messages = []
        warnings = []
        errors = []

        def add_message(to, tag, formatter):
            for el in test_case_el.getElementsByTagName(tag):
                path = el.attributes["file"].value
                try:
                    i = path.index(opts.project_path)
                    path = path[i:]
                except ValueError:
                    pass
                to.append(
                    "[[unimp]]{}[[rst]]:{} {}".format(
                        path, el.attributes["line"].value, strings_to_utf8(formatter(el) or "")
                    )
                )

        def format_exception(el):
            lines = [u"[[bad]]{}[[rst]]".format(el.childNodes[0].wholeText)]
            for el in el.getElementsByTagName("LastCheckpoint"):
                lines.append(
                    u"[[unimp]]{}[[rst]]:{} last checkpoint".format(
                        el.attributes["file"].value, el.attributes["line"].value
                    )
                )
            return u"\n".join(lines)

        add_message(messages, "Message", lambda el: el.childNodes and el.childNodes[0].wholeText)
        add_message(messages, "Info", lambda el: el.childNodes and el.childNodes[0].wholeText)
        add_message(
            warnings,
            "Warning",
            lambda el: el.childNodes and u"warning: [[warn]]{}[[rst]]".format(el.childNodes[0].wholeText),
        )
        add_message(
            errors, "Error", lambda el: el.childNodes and u"error: [[bad]]{}[[rst]]".format(el.childNodes[0].wholeText)
        )
        add_message(errors, "Exception", lambda el: el.childNodes and u"exception {}".format(format_exception(el)))
        add_message(
            errors,
            "FatalError",
            lambda el: el.childNodes and u"fatal error: [[bad]]{}[[rst]]".format(el.childNodes[0].wholeText),
        )

        if test_case_el.hasAttribute("skipped"):
            status = Status.SKIPPED
            snippet = test_case_el.attributes["reason"].value
        else:
            status = Status.GOOD if not errors else Status.FAIL
            snippet = "\n".join(messages + warnings + errors)

        test_name = get_test_name(test_case_el)

        logsdir = os.path.dirname(boost_log_path)
        test_log_path = get_test_log_file_path(logsdir, test_name)
        with open(test_log_path, "w") as log:
            log.write(yalibrary.display.strip_markup(snippet))

        test_case = facility.TestCase(
            test_name,
            status,
            snippet,
            elapsed=elapsed,
            path=opts.project_path,
            logs={'log': test_log_path, 'logsdir': logsdir},
        )
        suite.chunk.tests.append(test_case)


def run_tests(opts):
    if opts.tracefile:
        open(opts.tracefile, "w").close()

    binary = opts.binary
    log_path = os.path.join(opts.output_dir, "test_log.xml")
    report_path = os.path.join(opts.output_dir, "test_report.xml")
    suite = gen_suite(opts.project_path)
    cmd = [binary]
    run_params = ["--report_level=detailed", "--output_format=XML", "--log_level=test_suite"]
    for flt in opts.test_filter:
        flt = "::".join(flt.split("::")[1:])  # cut file name
        run_params += ["--run_test={}".format(flt.replace("::", "/"))]
    if not opts.android_app:
        cmd += ["--report_sink={}".format(report_path), "--log_sink={}".format(log_path)]
        cmd += run_params

    if opts.ios_app:
        device_name = "-".join([_f for _f in ["rnd", exts.uniq_id.gen16()] if _f])
        ios_simctl_control.prepare(
            opts.ios_simctl, opts.ios_profiles, os.getcwd(), device_name, cmd[0], opts.ios_device_type, opts.ios_runtime
        )
        exit_code = ios_simctl_control.run(
            opts.ios_simctl, opts.ios_profiles, os.getcwd(), device_name, cmd[1:]
        ).exit_code
        ios_simctl_control.cleanup(opts.ios_simctl, opts.ios_profiles, os.getcwd(), device_name)
    elif opts.android_app:
        if opts.android_arch not in ('i686', 'x86_64', 'armv8a'):
            raise Exception('Only i686, x86_64 and armv8a architectures supported for tests run')
        arch_map = {
            'i686': 'x86',
            'armv8a': 'arm64-v8a',
        }
        arch = arch_map.get(opts.android_arch, opts.android_arch)
        entry_point = opts.android_activity
        app_name = entry_point.split('/')[0]
        exit_code = 0
        device_name = "-".join([_f for _f in ["rnd", exts.uniq_id.gen16()] if _f])
        device_report_path = '/data/data/{}/files/report.xml'.format(app_name)
        device_log_path = '/data/data/{}/files/log.xml'.format(app_name)
        end_marker = '/data/data/{}/files/ya.end'.format(app_name)
        run_params += [
            '--report_sink={}'.format(device_report_path),
            '--log_sink={}'.format(device_log_path),
            '--ya-report={}'.format(end_marker),
        ]
        with android_control.AndroidEmulator(os.getcwd(), opts.android_sdk, opts.android_avd, arch) as android:
            logger.info("AVD name is {}".format(device_name))
            try:
                logger.info("Boot AVD")
                android.boot_device(device_name)
                for retry in range(ANDROID_MAX_RETRY_COUNT):
                    try:
                        logger.info("{} try".format(retry + 1))
                        logger.info("Install test apk")
                        android.install_app(device_name, binary, app_name)
                        logger.info("Run test apk")
                        android.run_test(device_name, entry_point, app_name, end_marker, run_params)
                        logger.info("Extract tests result")
                        # current x-86 emulator:
                        #     we can't pull file from /data/data/{app_name}/files/ (have no permission), but we can copy it to /sdcard/ and pull from there
                        # current x-86_64 emulator:
                        #     we can't copy report file to /sdcard/ (/sdcard/ is read-only), but we can pull it from /data/data/{app_name}/files/
                        android.extract_result(
                            device_name,
                            app_name,
                            [
                                (device_report_path, report_path),
                                (device_log_path, log_path),
                            ],
                        )
                        logger.info("Extracted")
                        break
                    except android_control.RetryableException as e:
                        if retry == ANDROID_MAX_RETRY_COUNT - 1:
                            raise
                        logger.warning(e)
            except Exception as e:
                print("Something went wrong", e, file=sys.stderr)
                exit_code = 1
    elif opts.gdb_debug:
        proc = shared.run_under_gdb(cmd, opts.gdb_path, None if opts.test_mode else '/dev/tty')
        proc.wait()
        exit_code = proc.returncode
    else:
        proc = process.execute(cmd, stderr=sys.stderr, wait=False)

        if hasattr(signal, "SIGUSR2"):

            def smooth_shutdown(signo, frame):
                os.kill(proc.process.pid, signal.SIGQUIT)
                _, status = subprocess._eintr_retry_call(os.waitpid, proc.process.pid, 0)
                proc.process._handle_exitstatus(status)

            signal.signal(signal.SIGUSR2, smooth_shutdown)

        try:
            proc.wait(check_exit_code=False)
        finally:
            exit_code = proc.exit_code
            if exit_code < 0:
                status = Status.TIMEOUT if exit_code == -signal.SIGQUIT else Status.FAIL
                core_path = cores.recover_core_dump_file(cmd[0], os.getcwd(), proc.process.pid)
                if core_path:
                    bt = cores.get_gdb_full_backtrace(cmd[0], core_path, opts.gdb_path)

                    bt_filename = os.path.join(opts.output_dir, 'backtrace.txt')
                    with open(os.path.join(opts.output_dir, 'backtrace.txt'), 'w') as afile:
                        afile.write(bt)

                    colorized_bt = "\n{}".format(cores.get_problem_stack(cores.colorize_backtrace(bt)))
                    suite.add_chunk_error('[[bad]]Problem thread backtrace:[[rst]]%s' % colorized_bt, status)
                    suite.chunk.logs['backtrace'] = bt_filename
                else:
                    suite.add_chunk_error("[[bad]]Test crashed with exit code: {}[[rst]]".format(exit_code), status)

    if os.path.exists(log_path):
        load_tests_from_log(opts, suite, log_path)

    if exit_code in [200, 201] and os.path.exists(report_path):
        with open(report_path) as f:
            error = f.read().strip() or "Test crashed"
            suite.add_chunk_error("[[bad]]{}[[rst]]".format(error))
    elif exit_code > 0:
        suite.add_chunk_error("[[bad]]Test crashed with exit code: {}[[rst]]".format(exit_code))

    suite.generate_trace_file(opts.tracefile)


def main():
    args = parse_args()
    setup_logging(args.verbose)

    if args.test_list:
        tests = get_tests(args)
        sys.stderr.write("\n".join(tests))
        return 0

    run_tests(args)

    return 0


if __name__ == "__main__":
    exit(main())
