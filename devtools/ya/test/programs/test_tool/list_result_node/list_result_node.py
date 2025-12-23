# coding: utf-8

"""
Print out test list
"""
import os
import json
import optparse
import collections

import devtools.ya.test.util.shared
import devtools.ya.test.const
from devtools.ya.test.common import ytest_common_tools as yct


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--test-name-filter", dest="test_name_filters", action='append', default=[])
    parser.add_option("--report-skipped-suites", dest="report_skipped_suites", action='store_true', default=False)
    parser.add_option(
        "--filter-description",
        dest="filter_description",
        default=None,
        help="Current tests filters description",
        action='store',
    )
    parser.add_option(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_option("--fail-exit-code", dest="fail_exit_code", help="exit code on fail", action='store', default='1')
    parser.add_option("--output", help="specifies the path to the file with test information")
    return parser.parse_args()


def get_number_of_empty_suites(projects):
    return len(projects) - len(remove_empty_projects(projects))


def remove_empty_projects(projects):
    '''
    Empty suites may appears if filtering requested
    when test node is launched with user specified filter and it doesn't contain required tests
    '''
    return [project for project in projects if project.get("tests") or project.get("error")]


def merge_suites(projects):
    '''
    Merge suites split after FORK_TEST_FILES
    '''
    result = collections.OrderedDict()
    for project in projects:
        key = "{} {} {}".format(project["project-path"], project["test-type"], project["target-platform-descriptor"])
        if key in result:
            stored = result[key]
            stored["tests"].extend(project["tests"])
            stored["error"] = "\n".join([_f for _f in [stored["error"], project["error"]] if _f])
        else:
            result[key] = project

    return result.values()


def sort_suites(projects):
    return sorted(projects, key=lambda x: "{} {}".format(x["project-path"], x["test-type"]))


def get_projects(report_skipped_suites):
    projects = []
    for project_path in devtools.ya.test.util.shared.get_projects_from_file("projects.txt"):
        list_json_path = os.path.join(project_path, devtools.ya.test.const.LIST_NODE_RESULT_FILE)
        with open(list_json_path) as tests_list_file:
            loaded = devtools.ya.test.common.strings_to_utf8(json.load(tests_list_file))
        projects.append(loaded)

    projects = merge_suites(projects)
    if not report_skipped_suites:
        projects = remove_empty_projects(projects)
    return sort_suites(projects)


def format_tags(tags):
    return " [tags: {}]".format(
        ", ".join(['[[warn]]{}[[rst]]'.format(t) if t.startswith("ya") else t for t in sorted(tags)])
    )


def main():
    # noinspection PyUnresolvedReferences
    import app_ctx

    display = app_ctx.display

    options, _ = get_options()
    devtools.ya.test.util.shared.setup_logging(options.log_level, devtools.ya.test.const.LIST_RESULT_NODE_LOG_FILE)

    projects = get_projects(options.report_skipped_suites)
    filter_message = devtools.ya.test.util.shared.build_filter_message(
        options.filter_description, options.test_name_filters, get_number_of_empty_suites(projects)
    )
    if filter_message:
        display.emit_message(filter_message)
        display.emit_message()

    if options.output:
        with open(options.output, "w") as afile:
            for data in projects:
                json.dump(data, afile)
                afile.write("\n")

    failed = False
    test_count = 0
    skipped_count = 0
    for project in projects:
        project_path = project["project-path"]
        test_type = project["test-type"]
        test_size = project["test-size"]
        suite_tags = project["test-tags"]
        target_platform_descriptor = project.get("target-platform-descriptor", None)
        list_error = project["error"]
        failed |= bool(list_error)

        message = '[[imp]]{}[[rst]] <[[unimp]]{}[[rst]]>'.format(project_path, test_type)
        if test_size and test_size != devtools.ya.test.const.TestSize.Small:
            message += " [size:[[imp]]{}[[rst]]]".format(test_size)
        if suite_tags:
            message += format_tags(suite_tags)
        if target_platform_descriptor:
            message += " for [[alt1]]{}[[rst]]".format(target_platform_descriptor)
        if list_error:
            message += "\n[[bad]]Test listing failed: {}[[rst]]".format(list_error)
        display.emit_message(message)

        tests = project["tests"]
        for t in tests:
            ti = yct.SubtestInfo.from_json(t)
            message = "  [[rst]]{}".format(str(ti))
            if ti.tags:
                message += format_tags(ti.tags)

            if ti.skipped:
                skipped_count += 1
            display.emit_message(message)
        test_count += len(tests)

    report = "{}Total {} suite{}".format('\n' if projects else '', len(projects), '' if len(projects) == 1 else 's')
    display.emit_message(report)

    report = "Total {} test{}".format(test_count, '' if test_count == 1 else 's')
    if skipped_count:
        report += ", {} skipped".format(skipped_count)
    display.emit_message(report)

    if failed:
        return int(options.fail_exit_code)
    return 0


if __name__ == '__main__':
    main()
