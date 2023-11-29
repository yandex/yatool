import collections
import logging
import optparse

import test.common
import test.const
import test.reports
import test.result
import test.test_types.common
import test.util.shared

logger = logging.getLogger(__name__)


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--test-name-filter", dest="test_name_filters", action='append', default=[])
    parser.add_option("--omitted-test-status", dest="omitted_test_statuses", action='append', default=[])
    parser.add_option("--filter-description", dest="filter_description", default=None, help="Current tests filters description", action='store')
    parser.add_option("--show-passed", default=False, dest="show_passed", help="Show passed tests", action='store_true')
    parser.add_option("--show-deselected", default=False, help="Show deselected tests", action='store_true')
    parser.add_option("--show-discovered", default=False, help="Show discovered tests", action='store_true')
    parser.add_option("--show-skipped", default=False, help="Show skipped tests", action='store_true')
    parser.add_option("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_option("--out-path", dest="out_path", help="test results out path", action='store')
    parser.add_option("--show-suite-logs-for-tags", dest="show_suite_logs_for_tags", action='append', default=[])
    parser.add_option("--result-root", help="result root", action='store'),
    parser.add_option("--source-root", help="source root", action='store', default="")
    parser.add_option(
        "--log-level", dest="log_level",
        help="logging level", action='store', default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"]
    )
    parser.add_option(
        "--fail-exit-code", dest="fail_exit_code", help="exit code on fail", action='store', default='1'
    )
    parser.add_option("--allure", dest="allure_report", help="allure file path", action='store', default=None)
    parser.add_option("--inline-diff", action="store_true", help="Don't extract diffs to files. Disable truncation of the comments printed on the terminal", default=False)
    parser.add_option("--show-test-cwd", action="store_true", help="show test cwd in the console report", default=False)
    parser.add_option("--show-metrics", action="store_true", help="show test metrics", default=False)
    parser.add_option("--show-failed", action="store_true", help="show failed only", default=False)
    return parser.parse_args()


def remove_empty_suites(suites):
    '''
    Empty suites may appears if filtering requested
    when test node is launched with user specified filter and it doesn't contain required tests
    '''
    return [suite for suite in suites if (suite._errors or suite.tests or any(c._errors for c in suite.chunks))]


def get_number_of_empty_suites(suites):
    return len(suites) - len(remove_empty_suites(suites))


def merge_suites(suites):
    '''
    Merge suites split after FORK_TEST_FILES
    '''
    result = collections.OrderedDict()
    for suite in suites:
        key = "{} {} {}".format(suite.project_path, suite.get_type(), suite.target_platform_descriptor)
        if key in result:
            stored = result[key]
            stored.tests.extend(suite.tests)
            stored._errors.extend(suite._errors)
        else:
            result[key] = suite

    return result.values()


def get_suites(options):
    outputs = test.util.shared.get_projects_from_file("projects.txt")
    logger.debug("Running result node for %s outputs", outputs)

    replacements = [
        ("$(BUILD_ROOT)", options.result_root),
        ("$(SOURCE_ROOT)", options.source_root or "arcadia-for-test"),
    ]
    resolver = test.reports.TextTransformer(replacements)

    suites = []
    for output in outputs:
        result = test.result.TestPackedResultView(output)
        suite = test.result.load_suite_from_result(result, output, resolver)
        if not suite.is_skipped() or options.show_discovered:
            suites.append(suite)

    suites = remove_empty_suites(suites)
    return merge_suites(suites)


def main():
    # noinspection PyUnresolvedReferences
    import app_ctx

    options, _ = get_options()
    test.util.shared.setup_logging(options.log_level, options.log_path)
    # ensure output file is created
    with open(options.out_path, "w") as f:
        f.write("")

    suites = get_suites(options)

    statuses = set([suite.get_status() for suite in suites if not suite.is_skipped()])
    if not statuses or statuses == {test.common.Status.GOOD}:
        exit_code = 0
        if options.show_failed:
            return exit_code
    elif test.common.Status.INTERNAL in statuses:
        exit_code = test.const.TestRunExitCode.InfrastructureError
    else:
        exit_code = int(options.fail_exit_code)

    if exit_code != 0:
        out_path = None  # will be stderr by default
    else:
        out_path = options.out_path

    truncate = not options.inline_diff
    reporter = test.reports.ConsoleReporter(
        show_passed=options.show_passed,
        show_deselected=options.show_deselected,
        show_skipped=options.show_skipped,
        show_test_cwd=options.show_test_cwd,
        show_metrics=options.show_metrics,
        truncate=truncate,
        omitted_test_statuses=options.omitted_test_statuses,
        show_suite_logs_for_tags=options.show_suite_logs_for_tags,
        out_path=out_path,
    )

    filter_message = test.util.shared.build_filter_message(
        options.filter_description,
        options.test_name_filters,
        get_number_of_empty_suites(suites))
    if filter_message:
        app_ctx.display.emit_message(filter_message)

    for suite in suites:
        reporter.on_test_suite_finish(suite)

    reporter.on_tests_finish(suites)

    return exit_code


if __name__ == '__main__':
    main()
