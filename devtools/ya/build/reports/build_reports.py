import collections
import os
import logging
import devtools.ya.test.util.shared as util_shared
import devtools.ya.test.reports.console as rc
import devtools.ya.test.test_node as test_node

from devtools.ya.test import common as test_common

CANONICAL_REPORT_TYPE = 'canonical'
HUMAN_READABLE_REPORT_TYPE = 'human_readable'

logger = logging.getLogger(__name__)


def fill_suites_results(builder, suites, output_root):
    import devtools.ya.test.result

    replacements = [
        ("$(BUILD_ROOT)", output_root),
        ("$(SOURCE_ROOT)", "arcadia"),
    ]
    resolver = devtools.ya.test.reports.TextTransformer(replacements)
    devtools.ya.test.result.fill_suites_results(suites, builder, output_root, resolver)

    return suites


def generate_results_report(builder):
    if not hasattr(builder.opts, "output_root"):
        return
    if not builder.opts.build_results_report_file and not builder.opts.junit_path:
        return

    import devtools.ya.test.util.tools
    import devtools.ya.core.error
    import devtools.ya.build.reports.autocheck_report as ar2

    result_root_link = devtools.ya.test.util.tools.get_log_results_link(builder.opts)
    output_dir = builder.opts.output_root
    assert output_dir
    output_dir = test_common.strings_to_utf8(output_dir)

    not_skipped_suites = []
    skipped_suites = []

    if builder.opts.report_skipped_suites_only or builder.opts.report_skipped_suites:
        skipped_suites = builder.ctx.stripped_tests
    if not builder.opts.report_skipped_suites_only:
        not_skipped_suites = fill_suites_results(builder, builder.ctx.tests, output_dir)

    suites = not_skipped_suites + skipped_suites

    report_prototype = collections.defaultdict(list)

    results_root = result_root_link or output_dir

    if builder.opts.junit_path:
        # don't generate junit for broken build if --keep-going isn't specified
        if builder.opts.continue_on_fail or builder.exit_code in [0, devtools.ya.core.error.ExitCodes.TEST_FAILED]:
            instance = devtools.ya.test.reports.JUnitReportGenerator()

            instance.create(
                builder.opts.junit_path,
                suites,
                lambda link: ar2._fix_link_prefix_and_quote(link, output_dir, results_root),
            )
            logger.info('Dump junit report to %s', builder.opts.junit_path)

    if builder.opts.build_results_report_file:
        results = ar2.fix_links(
            ar2.prepare_results(
                suites,
                report_prototype,
                builder,
                builder.get_owners(),
                builder.ctx.configure_errors,
                output_dir,
                output_dir,
            ),
            results_root,
        )

        report_type = builder.opts.build_report_type
        if report_type not in [CANONICAL_REPORT_TYPE, HUMAN_READABLE_REPORT_TYPE]:
            logger.warning("Unknown report type %s, will use %s type", report_type, CANONICAL_REPORT_TYPE)
            report_type = CANONICAL_REPORT_TYPE

        report_file_path = os.path.join(output_dir, builder.opts.build_results_report_file)
        logger.info('Dump results report to %s', report_file_path)
        if report_type == CANONICAL_REPORT_TYPE:
            from devtools.ya.yalibrary import sjson

            with open(report_file_path, 'wb') as rep_file:
                sjson.dump(results, rep_file)
        else:
            import exts.yjson as json

            with open(report_file_path, 'w') as rep_file:
                json.dump(results, rep_file, indent=4, sort_keys=True)


def generate_empty_tests_result_report(builder):
    if not builder.opts.run_tests:
        return
    if builder.ctx.tests:
        return
    stripped_tests = builder.ctx.stripped_tests
    if getattr(builder.opts, "list_tests", False):
        filter_message = util_shared.build_filter_message(
            ', '.join(test_node._get_skipped_tests_annotations(stripped_tests or [])),
            getattr(builder.opts, 'tests_filters', []) + getattr(builder.opts, 'test_files_filter', []),
            0,
        )
        if filter_message:
            rc.get_display().emit_message(filter_message)
            rc.get_display().emit_message()
        rc.get_display().emit_message("Total 0 suites")
