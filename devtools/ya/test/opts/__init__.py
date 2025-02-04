import logging
import os
import re
import shlex

import devtools.ya.core.yarg
from devtools.ya.core import error
from devtools.ya.core.yarg import help_level
from exts import path2
from devtools.ya.test import const

from library.python import func
from yalibrary.upload import consts as upload_consts

logger = logging.getLogger(__name__)

RUN_TEST_SUBGROUP = devtools.ya.core.yarg.Group(
    'Run tests', 1, desc='https://docs.yandex-team.ru/ya-make/usage/ya_make/tests'
)
FILTERING_SUBGROUP = devtools.ya.core.yarg.Group(
    'Filtering', 2, desc='https://docs.yandex-team.ru/ya-make/usage/ya_make/tests#test_filtering'
)
CONSOLE_REPORT_SUBGROUP = devtools.ya.core.yarg.Group('Console report', 3)
LINTERS_SUBGROUP = devtools.ya.core.yarg.Group(
    'Linters', 4, desc='https://docs.yandex-team.ru/ya-make/manual/tests/style'
)
CANONIZATION_SUBGROUP = devtools.ya.core.yarg.Group(
    'Canonization', 5, desc='https://docs.yandex-team.ru/ya-make/manual/tests/canon'
)
DEBUGGING_SUBGROUP = devtools.ya.core.yarg.Group('Debugging', 6)
RUNTIME_ENVIRON_SUBGROUP = devtools.ya.core.yarg.Group('Runtime environment', 7)
UID_CALCULATION_SUBGROUP = devtools.ya.core.yarg.Group('Test uid calculation', 8)
DEPS_SUBGROUP = devtools.ya.core.yarg.Group('Test dependencies', 9)
FILE_REPORTS_SUBGROUP = devtools.ya.core.yarg.Group('File reports', 10)
OUTPUT_SUBGROUP = devtools.ya.core.yarg.Group('Test outputs', 11)
TESTS_OVER_YT_SUBGROUP = devtools.ya.core.yarg.Group(
    'Tests over YT', 12, desc='https://docs.yandex-team.ru/devtools/test/yt'
)
TESTS_OVER_SANDBOX_SUBGROUP = devtools.ya.core.yarg.Group('Tests over Sandbox', 13)
COVERAGE_SUBGROUP = devtools.ya.core.yarg.Group(
    'Coverage', 14, desc='https://docs.yandex-team.ru/devtools/test/coverage'
)
FUZZ_SUBGROUP = devtools.ya.core.yarg.Group(
    'Fuzzing', 15, desc='https://docs.yandex-team.ru/ya-make/manual/tests/fuzzing'
)
# Test framework specific groups
PYTEST_SUBGROUP = devtools.ya.core.yarg.Group('Pytest specific', 16)
JAVA_SUBGROUP = devtools.ya.core.yarg.Group('Java tests specific', 17)
HERMIONE_SUBGROUP = devtools.ya.core.yarg.Group('Hermione specific', 18, desc='https://docs.yandex-team.ru/hermione')
JEST_SUBGROUP = devtools.ya.core.yarg.Group('Jest specific', 19)
JUNIT_SUBGROUP = devtools.ya.core.yarg.Group('JUnit specific', 20)
# Always last
TESTTOOL_SUBGROUP = devtools.ya.core.yarg.Group('Developer options', 100)


class TestArgConsumer(devtools.ya.core.yarg.ArgConsumer):
    def __init__(self, *args, **kw):
        kw['group'] = devtools.ya.core.yarg.TESTING_OPT_GROUP
        if kw.get('visible') is not False:
            assert 'subgroup' in kw, 'All test options must specify subgroup'
        super(TestArgConsumer, self).__init__(*args, **kw)


def test_options(
    cache_tests=False,
    test_size_filters=None,
    test_console_report=True,
    run_tests=0,
    run_tests_size=1,
    is_ya_test=False,
    strip_idle_build_results=False,
):
    # Order matters for options with same subgroup index
    return [
        RunTestOptions(run_tests=run_tests, run_tests_size=run_tests_size, is_ya_test=is_ya_test),
        ListingOptions(),
        ArcadiaTestsDataOptions(),
        CanonizationOptions(),
        ConsoleReportOptions(test_console_report),
        CoverageOptions(),
        DebuggingOptions(),
        DepsOptions(strip_idle_build_results=strip_idle_build_results),
        DistbuildOptions(),
        FileReportsOptions(),
        FilteringOptions(test_size_filters=test_size_filters),
        FuzzOptions(),
        InterimOptions(),
        InternalDebugOptions(),
        JavaOptions(),
        LintersOptions(),
        OutputOptions(),
        PytestOptions(),
        RuntimeEnvironOptions(),
        TestsOverSandboxOptions(),
        TestsOverYtOptions(),
        TestToolOptions(),
        UidCalculationOptions(cache_tests=cache_tests),
        HermioneOptions(),
        JUnitOptions(),
    ]


class RunTestOptions(devtools.ya.core.yarg.Options):
    HelpString = (
        'Run tests (-t runs only SMALL tests, -tt runs SMALL and MEDIUM tests, -ttt runs SMALL, MEDIUM and FAT tests)'
    )
    RunAllTests = 3

    def __init__(self, run_tests=0, run_tests_size=1, is_ya_test=False):
        self.run_tests = run_tests
        self.run_tests_size = run_tests_size
        self.peerdirs_test_type = 'none'
        self.fail_fast = False
        self.test_threads = 0
        self.testing_split_factor = 0
        self.test_prepare = False
        self._is_ya_test = is_ya_test
        self.cpu_detect_via_ram = True

    def consumer(self):
        peerdirs_test_types = ['none', 'gen', 'all']
        return [
            TestArgConsumer(
                ['-t', '--run-tests'],
                help=self.HelpString,
                hook=devtools.ya.core.yarg.UpdateValueHook('run_tests', lambda x: x + 1),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['-A', '--run-all-tests'],
                help='Run test suites of all sizes',
                hook=devtools.ya.core.yarg.SetConstValueHook('run_tests', self.RunAllTests),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.ConfigConsumer(
                'run_tests_size',
                help='Default test sizes to run (1 for small, 2 for small+medium, 3 to run all tests)',
            ),
            TestArgConsumer(
                ['--add-peerdirs-tests'],
                help='Peerdirs test types',
                hook=devtools.ya.core.yarg.SetValueHook('peerdirs_test_type', values=peerdirs_test_types),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--test-threads'],
                help='Restriction on concurrent tests (no limit by default)',
                hook=devtools.ya.core.yarg.SetValueHook('test_threads', int),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--fail-fast'],
                help='Fail after the first test failure',
                hook=devtools.ya.core.yarg.SetConstValueHook('fail_fast', True),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.ConfigConsumer('fail_fast'),
            TestArgConsumer(
                ['--split-factor'],
                help="Redefines SPLIT_FACTOR(X)",
                hook=devtools.ya.core.yarg.SetValueHook('testing_split_factor', transform=int),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--test-prepare'],
                help='Don\'t run tests, just prepare tests\' dependencies and environment',
                hook=devtools.ya.core.yarg.SetConstValueHook('test_prepare', True),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--remove-cpu-detect-via-ram'],
                help='Dont change test cpu requirements depending on ram',
                hook=devtools.ya.core.yarg.SetConstValueHook('cpu_detect_via_ram', False),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('cpu_detect_via_ram'),
        ]

    def postprocess(self):
        # use user desired default test size if '-t' is specified
        if self._is_ya_test and self.run_tests == 0:
            self.run_tests = self.RunAllTests
        if self.run_tests == 1:
            self.run_tests = max(1, min(self.RunAllTests, int(self.run_tests_size)))
        self.test_threads = max(0, self.test_threads)

    def postprocess2(self, params):
        if self.run_tests:
            for flags in ['flags', 'host_flags']:
                if hasattr(params, flags):
                    getattr(params, flags)['TESTS_REQUESTED'] = 'yes'
                    if self.peerdirs_test_type != 'none':
                        getattr(params, flags)['ADD_PEERDIRS_GEN_TESTS'] = 'yes'


class ListingOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.list_tests = False
        self.list_before_test = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['-L', '--list-tests'],
                help='List tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('list_tests', True),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--list-before-test'],
                help='pass list of tests before tests run',
                hook=devtools.ya.core.yarg.SetConstValueHook('list_before_test', True),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
        ]


class FilteringOptions(devtools.ya.core.yarg.Options):
    def __init__(self, test_size_filters=None):
        self.last_failed_tests = False
        self.test_files_filter = []
        self.test_size_filters = test_size_filters or []
        self.test_size_timeouts = {}
        self.test_tag_string = None
        self.test_tags_filter = []
        self.test_type_filters = []
        self.test_class_filters = []
        self.tests_filters = []
        self.tests_chunk_filters = []
        self.style = False
        self.regular_tests = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['-X', '--last-failed-tests'],
                help='Restart tests which failed in last run for chosen target',
                hook=devtools.ya.core.yarg.SetConstValueHook('last_failed_tests', True),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.ConfigConsumer('last_failed_tests'),
            TestArgConsumer(
                ['-F', '--test-filter'],
                help="Run only test that matches <tests-filter>. Asterics '*' can be used in filter to match test subsets. Chunks can be filtered as well using pattern that matches '[*] chunk'",
                hook=devtools.ya.core.yarg.SetAppendHook('tests_filters'),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--test-size'],
                help='Run only specified set of tests',
                hook=devtools.ya.core.yarg.SetAppendHook('test_size_filters'),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--test-type'],
                help='Run only specified types of tests',
                hook=devtools.ya.core.yarg.SetAppendHook('test_type_filters'),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--style'],
                help='Run only style tests and implies --strip-skipped-test-deps. Opposite of the --regular-tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('style', True),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--regular-tests'],
                help='Run only regular tests. Opposite of the --style',
                hook=devtools.ya.core.yarg.SetConstValueHook('regular_tests', True),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--test-tag'],
                help='Run tests that have specified tag',
                hook=devtools.ya.core.yarg.SetAppendHook('test_tags_filter'),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--test-filename'],
                help='Run only tests with specified filenames (pytest and hermione only)',
                hook=devtools.ya.core.yarg.SetAppendHook('test_files_filter'),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_TAG', help='Set tag filter', hook=devtools.ya.core.yarg.SetValueHook('test_tag_string')
            ),
            TestArgConsumer(
                ['--test-size-timeout'],
                help='Set test timeout for each size (small=60, medium=600, large=3600)',
                hook=devtools.ya.core.yarg.DictPutHook('test_size_timeouts'),
                subgroup=FILTERING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('test_size_timeouts'),
        ]

    def postprocess(self):
        if self.test_tag_string:
            self.test_tags_filter.append(self.test_tag_string)

        self.test_size_timeouts = {k.lower(): v for k, v in self.test_size_timeouts.items()}

        for key, value in tuple(self.test_size_timeouts.items()):
            key = key.lower()
            if key not in const.TestSize.sizes():
                raise devtools.ya.core.yarg.ArgsValidatingException("Invalid test size: {}".format(key))
            del self.test_size_timeouts[key]
            try:
                value = int(value)
                if value <= 0:
                    raise ValueError()
                self.test_size_timeouts[key] = value
            except ValueError:
                raise devtools.ya.core.yarg.ArgsValidatingException("Invalid timeout value: {}".format(value))

        if self.style and self.regular_tests:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "'--style' mode drops extra dependencies and thus incompatible with --regular-tests"
            )

        if self.regular_tests:
            self.test_class_filters = [const.SuiteClassType.REGULAR]

        if self.tests_filters:
            if self.last_failed_tests:
                filter_snippet = ' '.join(self.tests_filters)
                if len(filter_snippet) > 30:
                    filter_snippet = filter_snippet[:30] + '...'
                logger.warning("Specified filters '%s' extending filter -X/--last-failed-tests", filter_snippet)

            regex = re.compile(r"^\[.*?] chunk$")
            self.tests_chunk_filters, self.tests_filters = func.split(self.tests_filters, regex.search)

    def postprocess2(self, params):
        if params.flags.get("AUTOCHECK") == "yes" or params.run_tests < RunTestOptions.RunAllTests:
            tag_not_ya_autocheck = "ya:not_autocheck"
            for f in params.test_tags_filter:
                if tag_not_ya_autocheck in f:
                    break
            else:
                params.test_tags_filter.append("-{}".format(tag_not_ya_autocheck))

        # TODO remove when DEVTOOLS-6560 is done
        if params.test_files_filter:
            params.flags['FORK_TEST_FILES_FILTER'] = 'yes'

        if params.style:
            params.strip_skipped_test_deps = True
            params.test_class_filters = [const.SuiteClassType.STYLE]


class ConsoleReportOptions(devtools.ya.core.yarg.Options):
    def __init__(self, test_console_report=True):
        self.inline_diff = False
        self.max_test_comment_size = None
        self.omitted_test_statuses = ['good', 'xfail', 'not_launched']
        self.print_test_console_report = False
        self.show_deselected_tests = False
        self.show_metrics = False
        self.show_passed_tests = False
        self.show_skipped_tests = False
        self.test_console_report = test_console_report
        self.show_final_ok = True  # for internal usage by `ya run`

    def consumer(self):
        return [
            TestArgConsumer(
                ['-P', '--show-passed-tests'],
                help='Show passed tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('show_passed_tests', True),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--show-skipped-tests'],
                help='Show skipped tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('show_skipped_tests', True),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer('show_skipped_tests'),
            TestArgConsumer(
                ['--show-deselected-tests'],
                help='Show deselected tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('show_deselected_tests', True),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer('show_passed_tests'),
            TestArgConsumer(
                ['--inline-diff'],
                help="Disable truncation of the comments and print diff to the terminal",
                hook=devtools.ya.core.yarg.SetConstValueHook('inline_diff', True),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('inline_diff'),
            TestArgConsumer(
                ['--show-metrics'],
                help='Show metrics on console (You need to add "-P" option to see metrics for the passed tests)',
                hook=devtools.ya.core.yarg.SetConstValueHook('show_metrics', True),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--max-test-comment-size'],
                help='Set max test comment size',
                hook=devtools.ya.core.yarg.SetValueHook('max_test_comment_size'),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--skip-test-console-report'],
                help="Don't display test results on console",
                hook=devtools.ya.core.yarg.SetConstValueHook('test_console_report', False),
                subgroup=CONSOLE_REPORT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer(
                'omitted_test_statuses',
                help="List of test statuses omitted by default. Use '-P' to see all tests. Acceptable statuses: {}".format(
                    ', '.join(sorted(const.Status.BY_NAME.keys())),
                ),
            ),
        ]

    def postprocess(self):
        for status in self.omitted_test_statuses:
            if status not in const.Status.BY_NAME:
                raise devtools.ya.core.yarg.ArgsValidatingException(
                    "Unknown test status found (option omitted_test_statuses): {}".format(status)
                )

    def postprocess2(self, params):
        params.print_test_console_report = (
            params.run_tests
            and params.test_console_report
            and params.build_threads
            and not params.list_tests
            and not params.canonize_tests
            and params.remove_result_node
        )


class LintersOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.disable_flake8_migrations = True
        self.flake8_file_processing_delay = 1.5
        self.disable_jstyle_migrations = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--disable-flake8-migrations'],
                help='Enable all flake8 checks',
                hook=devtools.ya.core.yarg.SetConstValueHook('disable_flake8_migrations', True),
                subgroup=LINTERS_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('disable_flake8_migrations'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_DISABLE_FLAKE8_MIGRATIONS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'disable_flake8_migrations', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            TestArgConsumer(
                ['--flake8-file-processing-delay'],
                help='estimated time of flake8 check for 1 file. Deprecated and at some point will be replaced by -DFLAKE8_FILE_PROCESSING_TIME=XXX',
                hook=devtools.ya.core.yarg.SetValueHook('flake8_file_processing_delay', float),
                subgroup=LINTERS_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--disable-jstyle-migrations'],
                help='Enable all java style checks',
                hook=devtools.ya.core.yarg.SetConstValueHook('disable_jstyle_migrations', True),
                subgroup=LINTERS_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('disable_jstyle_migrations'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_DISABLE_JSTYLE_MIGRATIONS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'disable_jstyle_migrations', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
        ]

    def postprocess2(self, params):
        if params.disable_flake8_migrations:
            params.flags["DISABLE_FLAKE8_MIGRATIONS"] = "yes"


class CanonizationOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.canonization_transport = None
        self.canonize_tests = False
        self.test_diff = None
        self.canonization_backend = None
        self.canonization_scheme = "https"
        self.custom_canondata_path = None

    def consumer(self):
        return [
            TestArgConsumer(
                ['-Z', '--canonize-tests'],
                help='Canonize selected tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('canonize_tests', True),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--canonize-via-skynet'],
                help='use skynet to upload big canonical data',
                hook=devtools.ya.core.yarg.SetConstValueHook(
                    'canonization_transport', upload_consts.UploadTransport.Skynet
                ),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--canonize-via-http'],
                help='use http to upload big canonical data',
                hook=devtools.ya.core.yarg.SetConstValueHook(
                    'canonization_transport', upload_consts.UploadTransport.Http
                ),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--canon-diff'],
                help='Show test canonical data diff, allowed values are r<revision>, rev1:rev2, HEAD, PREV',
                hook=devtools.ya.core.yarg.SetValueHook('test_diff'),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--custom-canondata-path'],
                help='Store canondata in custom path instead of repository',
                hook=devtools.ya.core.yarg.SetValueHook('custom_canondata_path'),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--canonization-backend'],
                help='Allows to specify backend for canonical data with pattern',
                hook=devtools.ya.core.yarg.SetValueHook('canonization_backend'),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer('canonization_backend'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_CANONIZATION_BACKEND',
                hook=devtools.ya.core.yarg.SetValueHook('canonization_backend'),
            ),
            TestArgConsumer(
                ['--canonization-scheme'],
                help='Allows to specify canonization backend protocol(https by default)',
                hook=devtools.ya.core.yarg.SetValueHook('canonization_scheme'),
                subgroup=CANONIZATION_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer('canonization_scheme'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_CANONIZATION_SCHEME',
                hook=devtools.ya.core.yarg.SetValueHook('canonization_scheme'),
            ),
        ]

    def postprocess2(self, params):
        if params.custom_canondata_path:
            params.flags["CUSTOM_CANONDATA_PATH"] = params.custom_canondata_path


class DebuggingOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.debugger_requested = False
        self.gdb = False
        self.pdb = False
        self.dlv = False
        self.dlv_args = None
        self.random_ports = True
        self.test_allow_graceful_shutdown = True
        self.test_debug = False
        self.test_disable_timeout = False
        self.test_stderr = False
        self.test_stdout = False
        self.tests_retries = 1
        self.test_binary_args = []
        self.dump_test_environment = False
        self.show_test_cwd = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--pdb'],
                help='Start pdb on errors',
                hook=devtools.ya.core.yarg.SetConstValueHook('pdb', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--gdb'],
                help='Run c++ unittests in gdb',
                hook=devtools.ya.core.yarg.SetConstValueHook('gdb', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--dlv'],
                help='Run go unittests in dlv',
                hook=devtools.ya.core.yarg.SetConstValueHook('dlv', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--dlv-args'],
                help='Dlv extra command line options. Has no effect unless --dlv is also specified',
                hook=devtools.ya.core.yarg.SetValueHook('dlv_args'),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--tests-retries'],
                help='Alias for --test-retries',
                hook=devtools.ya.core.yarg.SetValueHook('tests_retries', int),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--test-retries'],
                help='Run every test specified number of times',
                hook=devtools.ya.core.yarg.SetValueHook('tests_retries', int),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--no-random-ports'],
                help='Use requested ports',
                hook=devtools.ya.core.yarg.SetConstValueHook('random_ports', False),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--test-stderr'],
                help='Output test stderr to console online',
                hook=devtools.ya.core.yarg.SetConstValueHook('test_stderr', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('test_stderr'),
            TestArgConsumer(
                ['--test-stdout'],
                help='Output test stdout to console online',
                hook=devtools.ya.core.yarg.SetConstValueHook('test_stdout', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer('test_stdout'),
            TestArgConsumer(
                ['--test-disable-timeout'],
                help='Turn off timeout for tests (only for local runs, incompatible with --cache-tests, --dist)',
                hook=devtools.ya.core.yarg.SetConstValueHook('test_disable_timeout', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_DISABLE_TIMEOUT',
                help='Turn off timeout for tests (only for local runs, incompatible with --cache-tests, --dist)',
                hook=devtools.ya.core.yarg.SetConstValueHook('test_disable_timeout', True),
            ),
            TestArgConsumer(
                ['--test-debug'],
                help='Test debug mode (prints test pid after launch and implies --test-threads=1 --test-disable-timeout --retest --test-stderr)',
                hook=devtools.ya.core.yarg.SetConstValueHook('test_debug', True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--disable-test-graceful-shutdown'],
                help="Test node will be killed immediately after the timeout",
                hook=devtools.ya.core.yarg.SetConstValueHook('test_allow_graceful_shutdown', False),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--test-binary-args'],
                help="Throw args to test binary",
                hook=devtools.ya.core.yarg.SetAppendHook("test_binary_args"),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--dump-test-environment'],
                help="List contents of test's build root in a tree-like format to the run_test.log file right before executing the test wrapper",
                hook=devtools.ya.core.yarg.SetConstValueHook("dump_test_environment", True),
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
        ]

    def postprocess(self):
        self.debugger_requested = self.gdb or self.pdb or self.dlv

    def postprocess2(self, params):
        if params.debugger_requested or params.test_debug:
            params.test_threads = 1
            params.test_disable_timeout = True
            params.cache_tests = False
            # Disable status refreshing for certain node
            params.status_refresh_interval = None
            params.show_test_cwd = True

        if params.show_test_cwd:
            params.test_stderr = True

        if params.tests_retries and params.tests_retries > 10 and params.use_distbuild:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "You cannot use --dist with --tests-retries when tests retries more then 10 (this creates a significant load on the distbuild)"
            )

        if params.debugger_requested and params.use_distbuild:
            raise devtools.ya.core.yarg.ArgsValidatingException("You cannot use interactive debuggers with --dist")


class RuntimeEnvironOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.collect_cores = True
        self.no_src_changes = False
        self.private_ram_drive = False
        self.private_net_ns = False
        self.autocheck_mode = False
        self.test_env = []
        self.test_params = {}

    def consumer(self):
        return [
            TestArgConsumer(
                ['--test-param'],
                help='Arbitrary parameters to be passed to tests (name=val)',
                hook=devtools.ya.core.yarg.DictPutHook('test_params'),
                subgroup=RUNTIME_ENVIRON_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--test-env'],
                help='Pass env variable key[=value] to tests. Gets value from system env if not set',
                hook=devtools.ya.core.yarg.SetAppendHook('test_env'),
                subgroup=RUNTIME_ENVIRON_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--no-src-changes'],
                help="Don't change source code",
                hook=devtools.ya.core.yarg.SetConstValueHook('no_src_changes', True),
                subgroup=RUN_TEST_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_NO_SRC_CHANGES', hook=devtools.ya.core.yarg.SetConstValueHook('no_src_changes', True)
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_COLLECT_CORES',
                hook=devtools.ya.core.yarg.SetValueHook('collect_cores', devtools.ya.core.yarg.return_true_if_enabled),
            ),
            TestArgConsumer(
                ['--autocheck-mode'],
                help="Run tests locally with autocheck restrictions (implies --private-ram-drive and --private-net-ns)",
                hook=devtools.ya.core.yarg.SetConstValueHook('autocheck_mode', True),
                subgroup=RUNTIME_ENVIRON_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--private-ram-drive'],
                help="Creates a private ram drive for all test nodes requesting one",
                hook=devtools.ya.core.yarg.SetConstValueHook('private_ram_drive', True),
                subgroup=RUNTIME_ENVIRON_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_PRIVATE_RAM_DRIVE',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'private_ram_drive', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            TestArgConsumer(
                ['--private-net-ns'],
                help="Creates a private network namespace with localhost support",
                hook=devtools.ya.core.yarg.SetConstValueHook('private_net_ns', True),
                subgroup=RUNTIME_ENVIRON_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_PRIVATE_NET_NS',
                hook=devtools.ya.core.yarg.SetValueHook('private_net_ns', devtools.ya.core.yarg.return_true_if_enabled),
            ),
        ]

    def postprocess(self):
        if self.autocheck_mode:
            self.private_ram_drive = True
            self.private_net_ns = True
            self.runner_dir_outputs = False

    def postprocess2(self, params):  # type: (Options) -> None
        if params.autocheck_mode:
            params.limit_build_root_size = True


class UidCalculationOptions(devtools.ya.core.yarg.Options):
    def __init__(self, cache_tests=False):
        self.cache_tests = cache_tests
        self.test_types_fakeid = {}
        self.test_fakeid = ""
        self.force_retest = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--cache-tests'],
                help='Use cache for tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('cache_tests', True),
                subgroup=UID_CALCULATION_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_CACHE_TESTS',
                hook=devtools.ya.core.yarg.SetValueHook('cache_tests', devtools.ya.core.yarg.return_true_if_enabled),
            ),
            devtools.ya.core.yarg.ConfigConsumer('cache_tests'),
            TestArgConsumer(
                ['--retest'],
                help='No cache for tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('force_retest', True),
                subgroup=UID_CALCULATION_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            # Allows to change test's uid per suite type
            devtools.ya.core.yarg.ConfigConsumer('test_types_fakeid'),
            devtools.ya.core.yarg.ConfigConsumer('test_fakeid'),
        ]

    def postprocess(self):
        if self.force_retest:
            self.cache_tests = False


class DepsOptions(devtools.ya.core.yarg.Options):
    def __init__(self, strip_idle_build_results=False):
        self.drop_graph_result_before_tests = False
        self.strip_skipped_test_deps = False
        self.strip_idle_build_results = strip_idle_build_results

    def consumer(self):
        return [
            TestArgConsumer(
                ['--strip-skipped-test-deps'],
                help="Don't build skipped test's dependencies",
                hook=devtools.ya.core.yarg.SetConstValueHook('strip_skipped_test_deps', True),
                subgroup=DEPS_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_STRIP_SKIPPED_TEST_DEPS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'strip_skipped_test_deps', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('strip_skipped_test_deps'),
            TestArgConsumer(
                ['--drop-graph-result-before-tests'],
                help="Build only targets required for requested tests",
                hook=devtools.ya.core.yarg.SetConstValueHook('drop_graph_result_before_tests', True),
                subgroup=DEPS_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--build-only-test-deps'],
                help="Build only targets required for requested tests",
                hook=devtools.ya.core.yarg.SetConstValueHook('drop_graph_result_before_tests', True),
                subgroup=DEPS_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_DROP_GRAPH_RESULT_BEFORE_TESTS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'drop_graph_result_before_tests', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            # TODO remove this option, see https://st.yandex-team.ru/YA-1898
            TestArgConsumer(
                ['--strip-idle-build-results'],
                help="Do not use this option, this is the default behavior",
                hook=devtools.ya.core.yarg.SetConstValueHook('strip_idle_build_results', True),
                subgroup=DEPS_SUBGROUP,
                visible=False,
                deprecated=True,
            ),
            # TODO remove this option, see https://st.yandex-team.ru/YA-1898
            TestArgConsumer(
                ['--no-strip-idle-build-results'],
                help="Use -b / --build-all instead of this option",
                hook=devtools.ya.core.yarg.SetConstValueHook('strip_idle_build_results', False),
                subgroup=DEPS_SUBGROUP,
                visible=False,
                deprecated=True,
            ),
            TestArgConsumer(
                ['-b', '--build-all'],
                help="Build targets that are not required to run tests, but are reachable with RECURSE's",
                hook=devtools.ya.core.yarg.SetConstValueHook('strip_idle_build_results', False),
                subgroup=DEPS_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            # TODO remove this option, see https://st.yandex-team.ru/YA-1898
            devtools.ya.core.yarg.EnvConsumer(
                'YA_STRIP_IDLE_BUILD_RESULTS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'strip_idle_build_results', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('strip_idle_build_results'),
        ]


class FileReportsOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.allure_report = None
        self.junit_path = None

    def consumer(self):
        return [
            TestArgConsumer(
                ['--allure'],
                help='Path to allure report to be generated',
                hook=devtools.ya.core.yarg.SetValueHook('allure_report'),
                subgroup=FILE_REPORTS_SUBGROUP,
                deprecated=True,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--junit'],
                help='Path to junit report to be generated',
                hook=devtools.ya.core.yarg.SetValueHook('junit_path'),
                subgroup=FILE_REPORTS_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.ConfigConsumer('junit_path'),
        ]

    def postprocess(self):
        if self.allure_report is not None:
            self.allure_report = path2.abspath(self.allure_report, expand_user=True)


class OutputOptions(devtools.ya.core.yarg.Options):
    CompressionFilters = ['none', 'zstd', 'gzip']

    def __init__(self):
        self.dir_outputs = True
        self.keep_full_test_logs = False
        self.output_only_tests = False
        self.save_test_outputs = True
        self.dir_outputs_in_nodes = False
        self.test_keep_symlinks = False
        self.test_output_compression_filter = 'zstd'
        self.test_output_compression_level = 1
        self.test_node_output_limit = None

    def consumer(self):
        return [
            TestArgConsumer(
                ['--output-only-tests'],
                help='Add only tests to the results root',
                hook=devtools.ya.core.yarg.SetConstValueHook('output_only_tests', True),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--no-test-outputs'],
                help="Don't save testing_out_stuff",
                hook=devtools.ya.core.yarg.SetConstValueHook('save_test_outputs', False),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_NO_TEST_OUTPUTS', hook=devtools.ya.core.yarg.SetConstValueHook('save_test_outputs', False)
            ),
            TestArgConsumer(
                ['--no-dir-outputs'],
                help="Tar testing output dir in the intermediate machinery",
                hook=devtools.ya.core.yarg.SetConstValueHook('dir_outputs', False),
                subgroup=OUTPUT_SUBGROUP,
                deprecated=True,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.ConfigConsumer('dir_outputs'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_DIR_OUTPUTS',
                hook=devtools.ya.core.yarg.SetValueHook('dir_outputs', devtools.ya.core.yarg.return_true_if_enabled),
            ),
            TestArgConsumer(
                ['--dir-outputs-in-nodes'],
                help="Enable dir outputs support in nodes",
                hook=devtools.ya.core.yarg.SetConstValueHook('dir_outputs_in_nodes', True),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.ConfigConsumer('dir_outputs_in_nodes'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_DIR_OUTPUTS_IN_NODES',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'dir_outputs_in_nodes', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            TestArgConsumer(
                ['--keep-full-test-logs'],
                help="Don't truncate logs on distbuild",
                hook=devtools.ya.core.yarg.SetConstValueHook('keep_full_test_logs', True),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--test-output-compression-filter'],
                help="Specifies compression filter for tos.tar",
                hook=devtools.ya.core.yarg.SetValueHook(
                    'test_output_compression_filter', values=self.CompressionFilters
                ),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_OUTPUT_COMPRESSION_FILTER',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'test_output_compression_filter', values=self.CompressionFilters
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('test_output_compression_filter'),
            TestArgConsumer(
                ['--test-output-compression-level'],
                help="Specifies compression level for tos.tar using specified compression filter",
                hook=devtools.ya.core.yarg.SetValueHook('test_output_compression_level', transform=int),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_OUTPUT_COMPRESSION_LEVEL',
                hook=devtools.ya.core.yarg.SetValueHook('test_output_compression_level', transform=int),
            ),
            devtools.ya.core.yarg.ConfigConsumer('test_output_compression_level'),
            TestArgConsumer(
                ['--test-node-output-limit'],
                help="Specifies output files limit(bytes)",
                hook=devtools.ya.core.yarg.SetValueHook('test_node_output_limit', transform=int),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.ConfigConsumer('test_node_output_limit'),
            TestArgConsumer(
                ['--test-keep-symlinks'],
                help="Don't delete symlinks from test output",
                hook=devtools.ya.core.yarg.SetConstValueHook('test_keep_symlinks', True),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
        ]

    def postprocess(self):
        if self.test_output_compression_filter == 'none':
            self.test_output_compression_filter = None
        else:
            if self.test_output_compression_level < 0:
                raise devtools.ya.core.yarg.ArgsValidatingException(
                    "Test output compression level cannot be negative: {}".format(self.test_output_compression_level)
                )

    def postprocess2(self, params):
        if params.dir_outputs:
            params.remove_tos = True
            params.merge_split_tests = False
            if params.use_distbuild or params.sandboxing or (params.cache_tests and not params.dir_outputs_test_mode):
                params.dir_outputs = False

        if params.autocheck_mode:
            params.runner_dir_outputs = False

        if params.keep_full_test_logs and not params.use_distbuild:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "Use --keep-full-test-logs with --dist. Local run don't truncate logs"
            )


class TestsOverYtOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.run_tagged_tests_on_yt = False
        self.ytexec_bin = None
        self.ytexec_wrapper_m_cpu = 250
        self.vanilla_execute_yt_token_path = None
        self.ytexec_title_suffix = None

    def consumer(self):
        return [
            TestArgConsumer(
                ['--run-tagged-tests-on-yt'],
                help="Run tests marked with ya:yt tag on the YT",
                hook=devtools.ya.core.yarg.SetConstValueHook('run_tagged_tests_on_yt', True),
                subgroup=TESTS_OVER_YT_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_RUN_TAGGED_TESTS_ON_YT',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'run_tagged_tests_on_yt', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer(
                'run_tagged_tests_on_yt',
            ),
            TestArgConsumer(
                ['--ytexec-bin'],
                help='use local ytexec binary',
                hook=devtools.ya.core.yarg.SetValueHook('ytexec_bin'),
                subgroup=TESTS_OVER_YT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--ytexec-wrapper-m-cpu'],
                help='specify millicpu requirements for distbuild.")',
                hook=devtools.ya.core.yarg.SetValueHook('ytexec_wrapper_m_cpu', int),
                subgroup=TESTS_OVER_YT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer('ytexec_wrapper_m_cpu'),
            TestArgConsumer(
                ['--ytexec-title-suffix'],
                help='pass additional information to operation title',
                hook=devtools.ya.core.yarg.SetValueHook('ytexec_title_suffix'),
                subgroup=TESTS_OVER_YT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--vanilla-execute-yt-token-path'],
                help='YT token path, which will be used to run tests with ya:yt TAG',
                hook=devtools.ya.core.yarg.SetValueHook('vanilla_execute_yt_token_path'),
                subgroup=TESTS_OVER_YT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_VANILLA_EXECUTE_YT_TOKEN_PATH',
                hook=devtools.ya.core.yarg.SetValueHook('vanilla_execute_yt_token_path'),
            ),
        ]


class TestsOverSandboxOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.force_create_frepkage = False
        self.frepkage_root = 'UNSPECIFIED'
        self.frepkage_target_uid = None
        self.run_tagged_tests_on_sandbox = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--run-tagged-tests-on-sandbox'],
                help="Run tests marked with ya:force_sandbox tag on the Sandbox",
                hook=devtools.ya.core.yarg.SetConstValueHook('run_tagged_tests_on_sandbox', True),
                subgroup=TESTS_OVER_SANDBOX_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_RUN_TAGGED_TESTS_ON_SANDBOX',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'run_tagged_tests_on_sandbox', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            TestArgConsumer(
                ['--frepkage-root'],
                help="Specified root of the frozen repository package",
                hook=devtools.ya.core.yarg.SetValueHook('frepkage_root'),
                subgroup=TESTS_OVER_SANDBOX_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_FREPKAGE_ROOT', hook=devtools.ya.core.yarg.SetValueHook('frepkage_root')
            ),
            TestArgConsumer(
                ['--force-create-frepkage'],
                help="Specifies path for frozen repository package and forces its creation even if -j0 is specified (required for testing)",
                hook=devtools.ya.core.yarg.SetValueHook('force_create_frepkage'),
                subgroup=TESTS_OVER_SANDBOX_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_FORCE_CREATE_FREPKAGE', hook=devtools.ya.core.yarg.SetValueHook('force_create_frepkage')
            ),
            TestArgConsumer(
                ['--frepkage-target-uid'],
                help="Strip graph from frepkage using target uid as single result uid",
                hook=devtools.ya.core.yarg.SetValueHook('frepkage_target_uid'),
                subgroup=TESTS_OVER_SANDBOX_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_FREPKAGE_TARGET_UID', hook=devtools.ya.core.yarg.SetValueHook('frepkage_target_uid')
            ),
        ]


class CoverageOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.build_coverage_report = False
        self.clang_coverage = False
        self.coverage = False
        self.coverage_direct_upload_yt = True
        self.coverage_exclude_regexp = None
        self.coverage_failed_upload_uids_file = None
        self.coverage_prefix_filter = None
        self.coverage_report_path = None
        self.coverage_succeed_upload_uids_file = None
        self.coverage_upload_snapshot_name = None
        self.coverage_verbose_resolve = False
        self.coverage_yt_token_path = None
        self.enable_contrib_coverage = False
        self.enable_java_contrib_coverage = False
        self.fast_clang_coverage_merge = False
        self.go_coverage = False
        self.java_coverage = False
        self.merge_coverage = False
        self.nlg_coverage = False
        self.python_coverage = False
        self.sancov_coverage = False
        self.ts_coverage = False
        self.upload_coverage_report = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--coverage'],
                help='Collect coverage information. (alias for "--clang-coverage --java-coverage --python-coverage --coverage-report")',
                hook=devtools.ya.core.yarg.SetConstValueHook('coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--coverage-prefix-filter'],
                help='Inspect only matched paths',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_prefix_filter'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--coverage-exclude-regexp'],
                help='Exclude matched paths from coverage report',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_exclude_regexp'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--coverage-report-path'],
                help='Path inside output dir where to store gcov cpp coverage report (use with --output)',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_report_path'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_COVERAGE_EXCLUDE_REGEXP',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_exclude_regexp'),
            ),
            TestArgConsumer(
                ['--python-coverage'],
                help='Collect python coverage information',
                hook=devtools.ya.core.yarg.SetConstValueHook('python_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--ts-coverage'],
                help='Collect ts coverage information',
                hook=devtools.ya.core.yarg.SetConstValueHook('ts_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--go-coverage'],
                help='Collect go coverage information',
                hook=devtools.ya.core.yarg.SetConstValueHook('go_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--java-coverage'],
                help='Collect java coverage information',
                hook=devtools.ya.core.yarg.SetConstValueHook('java_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--merge-coverage'],
                help='Merge all resolved coverage files to one file',
                hook=devtools.ya.core.yarg.SetConstValueHook('merge_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--sancov'],
                help='Collect sanitize coverage information (automatically increases tests timeout at {} times)'.format(
                    const.COVERAGE_TESTS_TIMEOUT_FACTOR
                ),
                hook=devtools.ya.core.yarg.SetConstValueHook('sancov_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--clang-coverage'],
                help="Clang's source based coverage (automatically increases tests timeout at {} times)".format(
                    const.COVERAGE_TESTS_TIMEOUT_FACTOR
                ),
                hook=devtools.ya.core.yarg.SetConstValueHook('clang_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--fast-clang-coverage-merge'],
                help="Merge profiles in the memory in test's runtime using fuse",
                hook=devtools.ya.core.yarg.SetConstValueHook('fast_clang_coverage_merge', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_FAST_CLANG_COVERAGE_MERGE',
                hook=devtools.ya.core.yarg.SetConstValueHook('fast_clang_coverage_merge', True),
            ),
            TestArgConsumer(
                ['--coverage-report'],
                help='Build HTML coverage report (use with --output)',
                hook=devtools.ya.core.yarg.SetConstValueHook('build_coverage_report', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--upload-coverage'],
                help='Upload collected coverage to the YT',
                hook=devtools.ya.core.yarg.SetConstValueHook('upload_coverage_report', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            TestArgConsumer(
                ['--coverage-upload-snapshot-name'],
                help='Use specified name for snapshot instead of svn revision',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_upload_snapshot_name'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--coverage-yt-token-path'],
                help='YT token path, which will be used to upload coverage',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_yt_token_path'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_COVERAGE_YT_TOKEN_PATH',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_yt_token_path'),
            ),
            TestArgConsumer(
                ['--coverage-save-uploaded-reports-uids-path'],
                help='Save uids of succeeded upload reports nodes',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_succeed_upload_uids_file'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--coverage-save-failed-reports-uids-path'],
                help='Save uids of failed upload reports nodes',
                hook=devtools.ya.core.yarg.SetValueHook('coverage_failed_upload_uids_file'),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--enable-java-contrib-coverage'],
                help='Add sources and classes from contib/java into jacoco report',
                hook=devtools.ya.core.yarg.SetConstValueHook('enable_java_contrib_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--enable-contrib-coverage'],
                help='Build contrib with coverage options and insert coverage.extractor tests for contrib binaries',
                hook=devtools.ya.core.yarg.SetConstValueHook('enable_contrib_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--nlg-coverage'],
                help='Collect Alice\'s NLG coverage information',
                hook=devtools.ya.core.yarg.SetConstValueHook('nlg_coverage', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--coverage-verbose-resolve'],
                help='Print debug logs during coverage resolve stage',
                hook=devtools.ya.core.yarg.SetConstValueHook('coverage_verbose_resolve', True),
                subgroup=COVERAGE_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
        ]

    def postprocess(self):
        if self.coverage:
            self.go_coverage = True
            self.clang_coverage = True
            self.java_coverage = True
            self.python_coverage = True
            self.ts_coverage = True
            # TODO backward compatibility - need to break it
            self.build_coverage_report = True

        if len(tuple(_f for _f in [self.sancov_coverage, self.clang_coverage] if _f)) > 1:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "You can collect only one type of cpp coverage at the same time"
            )

        if self.fast_clang_coverage_merge and not self.clang_coverage:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "You can use '--fast-clang-coverage-merge' only with '--clang-coverage' options"
            )

    def postprocess2(self, params):
        coverage_requested = False

        if params.go_coverage:
            params.flags['GO_TEST_COVER'] = 'yes'
            coverage_requested = True

        if params.python_coverage:
            params.flags['PYTHON_COVERAGE'] = 'yes'
            params.flags['CYTHON_COVERAGE'] = 'yes'
            coverage_requested = True

        if params.ts_coverage:
            params.flags['TS_COVERAGE'] = 'yes'
            coverage_requested = True

        if params.clang_coverage:
            params.flags['CLANG_COVERAGE'] = 'yes'
            coverage_requested = True

        if params.java_coverage:
            params.flags['JAVA_COVERAGE'] = 'yes'
            coverage_requested = True

        if params.nlg_coverage:
            params.flags['NLG_COVERAGE'] = 'yes'
            coverage_requested = True

        if params.enable_contrib_coverage:
            params.enable_java_contrib_coverage = True
            params.flags['ENABLE_CONTRIB_COVERAGE'] = 'yes'

        if params.sancov_coverage:
            params.sanitize_coverage = params.sanitize_coverage or 'trace-pc-guard,no-prune'
            params.sanitize = params.sanitize or 'address'
            coverage_requested = True

        if coverage_requested:
            if params.coverage_prefix_filter:
                params.flags['COVERAGE_TARGET_REGEXP'] = params.coverage_prefix_filter
            if params.coverage_exclude_regexp:
                params.flags['COVERAGE_EXCLUDE_REGEXP'] = params.coverage_exclude_regexp


class FuzzOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.fuzz_case_filename = None
        self.fuzz_force_minimization = False
        self.fuzz_local_store = False
        self.fuzz_minimization_only = False
        self.fuzz_node_timeout = None
        self.fuzz_opts = ''
        self.fuzz_runs = None
        self.fuzz_proof = 0
        self.fuzzing = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--fuzzing'],
                help="Extend test's corpus. Implies --sanitizer-flag=-fsanitize=fuzzer",
                hook=devtools.ya.core.yarg.SetConstValueHook('fuzzing', True),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--fuzz-opts'],
                help='Space separated string of fuzzing options',
                hook=devtools.ya.core.yarg.SetValueHook('fuzz_opts'),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--fuzz-case'],
                help='Specify path to the file with data for fuzzing (conflicting with "--fuzzing")',
                hook=devtools.ya.core.yarg.SetValueHook('fuzz_case_filename'),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--fuzz-minimization-only'],
                help='Allows to run minimization without fuzzing (should be used with "--fuzzing")',
                hook=devtools.ya.core.yarg.SetConstValueHook('fuzz_minimization_only', True),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--fuzz-local-store'],
                help="Don't upload mined corpus",
                hook=devtools.ya.core.yarg.SetConstValueHook('fuzz_local_store', True),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--fuzz-runs'],
                help="Minimal number of individual test runs",
                hook=devtools.ya.core.yarg.SetValueHook('fuzz_runs', transform=int),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--fuzz-proof'],
                help="Allows to run extra fuzzing stage with specified amount of seconds since last found case to proof that there would be no new case",
                hook=devtools.ya.core.yarg.SetValueHook('fuzz_proof', transform=int),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--fuzz-minimize'],
                help="Always run minimization node after fuzzing stage",
                hook=devtools.ya.core.yarg.SetConstValueHook('fuzz_force_minimization', True),
                subgroup=FUZZ_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
        ]

    @staticmethod
    def parse_fuzz_timeout(fuzz_opts):
        m = const.FUZZING_TIMEOUT_RE.search(fuzz_opts)
        if m:
            return int(m.group('max_time'))
        return const.FUZZING_DEFAULT_TIMEOUT

    def _get_fuzz_node_timeout(self, fuzz_opts):
        parsed_timeout = self.parse_fuzz_timeout(fuzz_opts)
        if parsed_timeout is not None:
            fuzz_proof = self.fuzz_proof or 0
            extra = int(os.environ.get("YA_FUZZING_FINISHING_TIME", "0")) or const.FUZZING_FINISHING_TIME
            return int(parsed_timeout * const.FUZZING_COMPRESSION_COEF + extra + fuzz_proof)
        return None

    def postprocess(self):
        if self.fuzzing and self.fuzz_case_filename:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "You can't specify certain case to check (--fuzz-case) when fuzzing is requested (--fuzzing)"
            )

        if self.fuzz_runs and not self.fuzzing:
            raise devtools.ya.core.yarg.ArgsValidatingException("Use can use --fuzz-runs only with --fuzzing")

        if self.fuzz_minimization_only and not self.fuzzing:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                "Use can use --fuzz-minimization-only only with --fuzzing, otherwise minimization nodes won't be injected into the graph"
            )

        if self.fuzz_force_minimization and not self.fuzzing:
            raise devtools.ya.core.yarg.ArgsValidatingException("Use can use --fuzz-minimize only with --fuzzing")

    def postprocess2(self, params):
        if params.fuzzing:
            params.flags['FUZZING'] = 'yes'
            params.test_type_filters = ['fuzz']
            params.test_tags_filter = [const.ServiceTags.AnyTag]
            params.fuzz_node_timeout = self._get_fuzz_node_timeout(params.fuzz_opts)

            if not any(x for x in params.sanitizer_flags if x == '-fsanitize=fuzzer'):
                params.sanitizer_flags.append('-fsanitize=fuzzer')


class HermioneOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.hermione_config = None
        self.hermione_grep = None
        self.hermione_test_paths = []
        self.hermione_browsers = []
        self.hermione_sets = []
        self.hermione_gui = False
        self.hermione_gui_auto_run = False
        self.hermione_gui_no_open = False
        self.hermione_gui_hostname = None
        self.hermione_gui_port = None

    def consumer(self):
        return [
            TestArgConsumer(
                ['--hermione-config'],
                help="Path to configuration file",
                hook=devtools.ya.core.yarg.SetValueHook('hermione_config'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--hermione-grep'],
                help="Run tests that only matching by specified pattern",
                hook=devtools.ya.core.yarg.SetValueHook('hermione_grep'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-test-path'],
                help="Run tests that are in the specified files (paths must be relative to cwd)",
                hook=devtools.ya.core.yarg.SetAppendHook('hermione_test_paths'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-browser'],
                help="Run tests only in specified browser",
                hook=devtools.ya.core.yarg.SetAppendHook('hermione_browsers'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            TestArgConsumer(
                ['--hermione-set'],
                help="Run tests only in specified set",
                hook=devtools.ya.core.yarg.SetAppendHook('hermione_sets'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-gui'],
                help="Run hermione in gui mode",
                hook=devtools.ya.core.yarg.SetConstValueHook('hermione_gui', True),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-gui-auto-run'],
                help="Auto run tests in gui mode immediately",
                hook=devtools.ya.core.yarg.SetConstValueHook('hermione_gui_auto_run', True),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-gui-no-open'],
                help="Not to open a browser window after starting the server in gui mode",
                hook=devtools.ya.core.yarg.SetConstValueHook('hermione_gui_no_open', True),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-gui-hostname'],
                help='Gui hostname to launch server on',
                hook=devtools.ya.core.yarg.SetValueHook('hermione_gui_hostname'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--hermione-gui-port'],
                help='Gui port to launch server on',
                hook=devtools.ya.core.yarg.SetValueHook('hermione_gui_port'),
                subgroup=HERMIONE_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
        ]


class PytestOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.profile_pytest = False
        self.test_log_level = None
        self.test_traceback = "short"
        self.pytest_args = []

    def consumer(self):
        return [
            TestArgConsumer(
                ['--test-log-level'],
                help='Specifies logging level for output test logs',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'test_log_level', values=["critical", "error", "warning", "info", "debug"]
                ),
                subgroup=PYTEST_SUBGROUP,
                visible=help_level.HelpLevel.BASIC,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_LOG_LEVEL',
                help='Specifies logging level for output test logs',
                hook=devtools.ya.core.yarg.SetValueHook('test_log_level'),
            ),
            TestArgConsumer(
                ['--test-traceback'],
                help='Test traceback style for pytests',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'test_traceback', values=["long", "short", "line", "native", "no"]
                ),
                subgroup=PYTEST_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer(
                'test_traceback',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'test_traceback', values=["long", "short", "line", "native", "no"]
                ),
            ),
            TestArgConsumer(
                ['--profile-pytest'],
                help="Profile pytest (dumps cProfile to the stderr and generates 'pytest.profile.dot' using gprof2dot in the testing_out_stuff directory)",
                hook=devtools.ya.core.yarg.SetConstValueHook('profile_pytest', True),
                subgroup=PYTEST_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--pytest-args'],
                help="Pytest extra command line options",
                hook=devtools.ya.core.yarg.SetValueHook('pytest_args', transform=shlex.split),
                subgroup=PYTEST_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
        ]


class JavaOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.ignored_properties_files = []
        self.jstyle_runner_path = None
        self.jvm_args = None
        self.properties = {}
        self.properties_files = []

    def consumer(self):
        return [
            TestArgConsumer(
                ['--jstyle-runner-path'],
                help="Path to custom runner for java style tests",
                hook=devtools.ya.core.yarg.SetValueHook('jstyle_runner_path'),
                subgroup=JAVA_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            TestArgConsumer(
                ['--system-property'],
                help='Set system property (name=val)',
                hook=devtools.ya.core.yarg.DictPutHook('properties'),
                subgroup=JAVA_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--system-properties-file'],
                help='Load system properties from file',
                hook=devtools.ya.core.yarg.SetAppendHook('properties_files'),
                subgroup=JAVA_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            TestArgConsumer(
                ['--jvm-args'],
                help='Add jvm args for jvm launch',
                hook=devtools.ya.core.yarg.SetValueHook('jvm_args'),
                subgroup=JAVA_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.EnvConsumer('YA_JVM_ARGS', hook=devtools.ya.core.yarg.SetValueHook('jvm_args')),
        ]

    def postprocess2(self, params):
        if self.jstyle_runner_path:
            for flags in ['flags', 'host_flags']:
                if hasattr(params, flags):
                    getattr(params, flags)['USE_SYSTEM_JSTYLE_LIB'] = self.jstyle_runner_path


class DistbuildOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.backup_test_results = False

    def consumer(self):
        return [
            TestArgConsumer(
                ['--backup-test-results'],
                help='Backup test results on the distbuild',
                hook=devtools.ya.core.yarg.SetConstValueHook('backup_test_results', True),
                subgroup=OUTPUT_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
        ]


class TestToolOptions(devtools.ya.core.yarg.Options):
    visible = help_level.HelpLevel.INTERNAL

    def __init__(self):
        self.test_tool_bin = None
        self.test_tool3_bin = None
        self.profile_test_tool = []

    def consumer(self):
        return [
            TestArgConsumer(
                ['--test-tool-bin'],
                help='Path to test_tool binary',
                hook=devtools.ya.core.yarg.SetValueHook('test_tool_bin'),
                subgroup=TESTTOOL_SUBGROUP,
            ),
            TestArgConsumer(
                ['--profile-test-tool'],
                help="Profile specified test_tool handlers",
                hook=devtools.ya.core.yarg.SetAppendHook('profile_test_tool'),
                subgroup=TESTTOOL_SUBGROUP,
            ),
        ]

    def postprocess2(self, params):
        if self.test_tool_bin:
            for flags in ['flags', 'host_flags']:
                if hasattr(params, flags):
                    getattr(params, flags)['TEST_TOOL_HOST_LOCAL'] = self.test_tool_bin
                    getattr(params, flags)['TEST_TOOL_TARGET_LOCAL'] = self.test_tool_bin


class InterimOptions(devtools.ya.core.yarg.Options):
    Visible = False

    # All this options will be removed when work is done
    def __init__(self):
        self.cache_fs_read = False
        self.cache_fs_write = False
        self.merge_split_tests = True
        self.remove_result_node = True
        self.remove_tos = False
        self.test_fail_exit_code = error.ExitCodes.TEST_FAILED
        self.detect_leaks_in_pytest = True
        self.fail_maven_export_with_tests = False
        self.use_jstyle_server = False
        self.setup_pythonpath_env = True
        self.use_command_file_in_testtool = False
        self.use_throttling = False
        self.remove_implicit_data_path = False
        self.no_tests_is_error = False
        self.tests_limit_in_suite = 100000

    def consumer(self):
        return [
            TestArgConsumer(
                ['--dont-merge-split-tests'],
                help="Don't merge split tests testing_out_stuff dir (with macro FORK_*TESTS)",
                hook=devtools.ya.core.yarg.SetConstValueHook('merge_split_tests', False),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_MERGE_SPLIT_TESTS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'merge_split_tests', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('merge_split_tests'),
            TestArgConsumer(
                ['--remove-result-node'],
                help='remove result node from graph, print test report in ya and report skipped suites after configure',
                hook=devtools.ya.core.yarg.SetConstValueHook('remove_result_node', True),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.ConfigConsumer('remove_result_node'),
            devtools.ya.core.yarg.EnvConsumer(
                "YA_TEST_REMOVE_RESULT_NODE",
                help="remove result node",
                hook=devtools.ya.core.yarg.SetValueHook(
                    "remove_result_node", devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            TestArgConsumer(
                ['--remove-tos'],
                help='remove top level testing_out_stuff directory',
                hook=devtools.ya.core.yarg.SetConstValueHook('remove_tos', True),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.ConfigConsumer('remove_tos'),
            devtools.ya.core.yarg.EnvConsumer(
                "YA_TEST_REMOVE_TOS",
                help="remove top level tos directory",
                hook=devtools.ya.core.yarg.SetValueHook("remove_tos"),
            ),
            TestArgConsumer(
                ['--cache-fs-read'],
                help='Use FS cache instead memory cache (only read)',
                hook=devtools.ya.core.yarg.SetConstValueHook('cache_fs_read', True),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.ConfigConsumer('cache_fs_read'),
            TestArgConsumer(
                ['--cache-fs-write'],
                help='Use FS cache instead memory cache (only write)',
                hook=devtools.ya.core.yarg.SetConstValueHook('cache_fs_write', True),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.ConfigConsumer('cache_fs_write'),
            TestArgConsumer(
                ['--test-failure-code'],
                help='Exit code when tests fail',
                hook=devtools.ya.core.yarg.SetValueHook('test_fail_exit_code'),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_TEST_FAILURE_CODE', hook=devtools.ya.core.yarg.SetValueHook('test_fail_exit_code')
            ),
            devtools.ya.core.yarg.ConfigConsumer('detect_leaks_in_pytest'),
            # See DEVTOOLS-9388
            devtools.ya.core.yarg.EnvConsumer(
                'YA_FAIL_MAVEN_EXPORT_WITH_TESTS',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'fail_maven_export_with_tests', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('fail_maven_export_with_tests'),
            devtools.ya.core.yarg.ConfigConsumer('use_jstyle_server'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_USE_JSTYLE_SERVER',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'use_jstyle_server', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_SETUP_PYTHONPATH_ENV',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'setup_pythonpath_env', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('setup_pythonpath_env'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_USE_COMMAND_FILE_IN_TESTTOOL',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'use_command_file_in_testtool', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            devtools.ya.core.yarg.ConfigConsumer('use_command_file_in_testtool'),
            devtools.ya.core.yarg.ConfigConsumer('use_throttling'),
            # remove implicit data path from DATA
            TestArgConsumer(
                ['--remove-implicit-data-path'],
                help='Remove implicit path from DATA macro',
                hook=devtools.ya.core.yarg.SetConstValueHook('remove_implicit_data_path', False),
                visible=self.Visible,
            ),
            TestArgConsumer(
                ['--dont-remove-implicit-data-path'],
                help='Set implicit path to DATA with ya',
                hook=devtools.ya.core.yarg.SetConstValueHook('remove_implicit_data_path', True),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.ConfigConsumer('remove_implicit_data_path'),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_REMOVE_IMPLICIT_DATA_PATH',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'remove_implicit_data_path', devtools.ya.core.yarg.return_true_if_enabled
                ),
            ),
            TestArgConsumer(
                ['--no-tests-is-error'],
                help='Return a special exit code if tests were requested, but no tests were run',
                hook=devtools.ya.core.yarg.SetConstValueHook('no_tests_is_error', True),
                visible=self.Visible,
            ),
            devtools.ya.core.yarg.ConfigConsumer('no_tests_is_error'),
            devtools.ya.core.yarg.ConfigConsumer('tests_limit_in_suite'),
        ]

    def postprocess2(self, params):
        if (
            getattr(params, 'export_to_maven', False)
            and getattr(params, 'run_tests', 0)
            and params.fail_maven_export_with_tests
        ):
            raise devtools.ya.core.yarg.ArgsValidatingException("Export to maven is not allowed with running tests")


class InternalDebugOptions(devtools.ya.core.yarg.Options):
    Visible = False

    def __init__(self):
        self.cache_test_statuses = None
        self.skip_cross_compiled_tests = True
        self.propagate_test_timeout_info = False
        self.ytexec_node_timeout = None
        self.store_original_tracefile = False

    def consumer(self):
        # Internal debug options must stay internal.
        # Each option must be invisible and used only in devtools tests
        return [
            TestArgConsumer(
                ['--cache-test-statuses'],
                help='cache last failed tests statuses',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'cache_test_statuses', devtools.ya.core.yarg.return_true_if_enabled
                ),
                visible=self.Visible,
            ),
            TestArgConsumer(
                ['--ytexec-node-timeout'],
                help='Set ytexec node timeout for local run',
                hook=devtools.ya.core.yarg.SetValueHook('ytexec_node_timeout', int),
                subgroup=TESTS_OVER_YT_SUBGROUP,
                visible=self.Visible,
            ),
            TestArgConsumer(
                ['--skip-cross-compiled-tests'],
                help='Allows do not strip cross-compiled test from graph for test purposes',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'skip_cross_compiled_tests', devtools.ya.core.yarg.return_true_if_enabled
                ),
                visible=self.Visible,
            ),
            TestArgConsumer(
                ['--propagate-test-timeout-info'],
                help='Report the list of involved tests for not launched tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('propagate_test_timeout_info', True),
                visible=self.Visible,
            ),
            TestArgConsumer(
                ['--store-original-tracefile'],
                hook=devtools.ya.core.yarg.SetConstValueHook('store_original_tracefile', True),
                help="Store original trace file",
                subgroup=DEBUGGING_SUBGROUP,
                visible=help_level.HelpLevel.INTERNAL,
            ),
            devtools.ya.core.yarg.ConfigConsumer('store_original_tracefile'),
        ]


class ArcadiaTestsDataOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.arcadia_tests_data_path = 'arcadia_tests_data'

    def consumer(self):
        return [
            TestArgConsumer(
                ['--arcadia-tests-data'],
                help='Custom path to arcadia_tests_data',
                hook=devtools.ya.core.yarg.SetValueHook('arcadia_tests_data_path'),
                subgroup=RUNTIME_ENVIRON_SUBGROUP,
                visible=help_level.HelpLevel.EXPERT,
            ),
            devtools.ya.core.yarg.EnvConsumer(
                'YA_ARCADIA_TESTS_DATA',
                help='Custom path to arcadia_tests_data',
                hook=devtools.ya.core.yarg.SetValueHook('arcadia_tests_data_path'),
            ),
        ]


class JUnitOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.junit_args = None

    def consumer(self):
        return [
            TestArgConsumer(
                ['--junit-args'],
                help='JUnit extra command line options',
                hook=devtools.ya.core.yarg.SetValueHook('junit_args', transform=shlex.split),
                subgroup=JUNIT_SUBGROUP,
                visible=help_level.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ConfigConsumer(
                "junit_args",
                hook=devtools.ya.core.yarg.SetValueHook('junit_args', transform=shlex.split),
            ),
        ]
