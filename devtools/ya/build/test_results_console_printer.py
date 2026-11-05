import devtools.ya.test.const
import devtools.ya.test.reports
from devtools.ya.test.util.shared import build_filter_message


def get_skipped_tests_annotations(suites):
    by_message = {}
    for suite in suites:
        if suite.is_skipped():
            skipped_msg = suite.get_comment()
            if skipped_msg not in by_message:
                by_message[skipped_msg] = 0
            by_message[skipped_msg] += 1
    return ["{}: {}".format(msg, count) for msg, count in by_message.items()]


def is_empty_suite(suite):
    return not suite.has_comment() and not any(c.has_comment() for c in suite.chunks) and not suite.tests


def remove_empty_suites(suites):
    '''
    Empty suites may appears if filtering requested
    when test node is launched with user specified filter and it doesn't contain required tests
    '''
    return [suite for suite in suites if not is_empty_suite(suite)]


def get_number_of_empty_suites(suites):
    return len(suites) - len(remove_empty_suites(suites))


def get_suites_to_show(suites, fail_fast=False, report_skipped_suites=False):
    suites_to_show = [s for s in suites if not s.is_skipped() or report_skipped_suites]
    suites_to_show = remove_empty_suites(suites_to_show)
    if fail_fast:
        try:
            suites_to_show = [
                [suite for suite in suites_to_show if suite.get_status() != devtools.ya.test.const.Status.GOOD][0]
            ]
        except IndexError:
            pass
    return suites_to_show


def _make_reporter(opts, display):
    if getattr(display, 'structured', False):
        return devtools.ya.test.reports.JsonlReporter(
            display,
            omitted_test_statuses=opts.omitted_test_statuses,
            show_deselected=opts.show_deselected_tests,
            show_skipped=opts.show_skipped_tests,
            truncate=not opts.inline_diff,
            arc_root=opts.arc_root,
            fail_fast=opts.fail_fast,
            last_failed_tests=opts.last_failed_tests,
            show_slowest=opts.show_slowest_tests,
        )
    return devtools.ya.test.reports.ConsoleReporter(
        show_passed=opts.show_passed_tests,
        show_deselected=opts.show_deselected_tests,
        show_skipped=opts.show_skipped_tests,
        show_test_cwd=opts.keep_temps,
        show_metrics=opts.show_metrics,
        truncate=not opts.inline_diff,
        omitted_test_statuses=opts.omitted_test_statuses,
        show_suite_logs_for_tags=(
            [devtools.ya.test.const.YaTestTags.ForceSandbox] if opts.run_tagged_tests_on_sandbox else []
        ),
        display=display,
        show_slowest=opts.show_slowest_tests,
    )


def print_tests_results_to_console(opts, display, suites):
    filter_description = ", ".join(get_skipped_tests_annotations(suites))
    suites_to_show = get_suites_to_show(suites, opts.fail_fast, opts.report_skipped_suites)
    reporter = _make_reporter(opts, display)

    filter_message = build_filter_message(
        filter_description, opts.tests_filters, get_number_of_empty_suites(suites_to_show)
    )

    if filter_message:
        reporter.on_warning(filter_message)

    for suite in suites_to_show:
        reporter.on_test_suite_finish(suite)

    reporter.on_tests_finish(suites_to_show)
