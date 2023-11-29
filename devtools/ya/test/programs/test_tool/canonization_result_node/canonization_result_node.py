# coding: utf-8

import os
import json
import optparse

import test.const
import test.util.shared


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_option("--test-name-filter", dest="test_name_filters", action='append', default=[])
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
    return parser.parse_args()


def get_number_of_empty_suites(projects):
    return len([project for project in projects if not project.get("tests_count")])


def load_projects():
    project_paths = test.util.shared.get_projects_from_file("projects.txt")
    projects = []
    for path in project_paths:
        canonization_res = os.path.join(path, test.const.CANONIZATION_RESULT_FILE_NAME)
        with open(canonization_res) as tests_list_file:
            projects.append(json.load(tests_list_file))
    return projects


def main():
    # noinspection PyUnresolvedReferences
    import app_ctx

    options, _ = get_options()
    test.util.shared.setup_logging(options.log_level, options.log_path)

    projects = load_projects()

    filter_message = test.util.shared.build_filter_message(
        options.filter_description, options.test_name_filters, get_number_of_empty_suites(projects)
    )
    if filter_message:
        app_ctx.display.emit_message(filter_message)

    for project in projects:
        if not project["status"]:
            return 1
    return 0
