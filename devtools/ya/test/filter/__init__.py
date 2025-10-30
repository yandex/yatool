# coding: utf-8

import re
import os
import fnmatch
import logging
import inspect
from typing import Callable

import devtools.ya.test.const
import exts.func

import devtools.ya.test.test_types.common as common_suites

logger = logging.getLogger(__name__)


@exts.func.lazy
def get_tag_regex():
    return re.compile(r"([+\-]?[\w:]*)")


@exts.func.lazy
def get_test_type_regex():
    return re.compile(r"([+\-]?[\w\.]*)")


class FilterException(Exception):
    mute = True


def fix_filter(flt):
    if devtools.ya.test.const.TEST_SUBTEST_SEPARATOR not in flt and "*" not in flt:
        # user wants to filter by test module name
        flt = flt + devtools.ya.test.const.TEST_SUBTEST_SEPARATOR + "*"
    return flt


def escape_for_fnmatch(s):
    return s.replace("[", "&#91;").replace("]", "&#93;")


def make_name_filter(filter_names):
    filter_full_names = set()
    for name in filter_names:
        if '*' not in name:
            filter_full_names.add(name)

    def predicate(testname):
        return testname in filter_full_names or any(
            [
                fnmatch.fnmatch(escape_for_fnmatch(testname), escape_for_fnmatch(filter_name))
                for filter_name in filter_names
            ]
        )

    return predicate


def make_testname_filter(filter_names):
    filter_names = list(map(fix_filter, filter_names))
    return make_name_filter(filter_names)


def timeout_filter(max_timeout):
    def filter_function(test_suite):
        return max_timeout is None or test_suite.declared_timeout <= max_timeout

    return filter_function


def filter_test_size(test_size_filters):
    if test_size_filters:

        def filter_function(test_suite):
            return test_suite.test_size in test_size_filters

        return filter_function
    else:
        return lambda x: True


def filter_suite_class_type(test_class_filters):
    if test_class_filters:

        def filter_function(test_suite):
            return test_suite.class_type in test_class_filters

        return filter_function
    else:
        return lambda x: True


def filter_suite_type(type_filters):
    if "pytest" in type_filters:
        type_filters.append("py3test")
    elif "py2test" in type_filters:
        type_filters.remove("py2test")
        type_filters.append("pytest")
    elif "py3test" in type_filters:
        pass

    if type_filters:
        include, exclude = parse_simple_set_op_notation(get_test_type_regex(), type_filters)

        def filter_function(test_suite):
            suite_type = test_suite.get_type()
            fit = True
            if include:
                fit = suite_type in include
            if fit and exclude:
                fit = suite_type not in exclude
            return fit

        return filter_function
    else:
        return lambda x: True


def empty_tests_filter(test_suites):
    return any([len(t.subtests) for t in test_suites.tests])


def parse_simple_set_op_notation(regex, data_set):
    include, exclude = set(), set()

    for line in data_set:
        for expression in [_f for _f in regex.findall(line) if _f]:
            if expression:
                operation = expression[0]
                if operation in ("+", "-"):
                    expression = expression[1:]

                if operation == "-":
                    exclude.add(expression)
                else:
                    # Treat expression without operation as an include
                    include.add(expression)

    return include, exclude


def tags_filter(tags, list_mode=False):
    include, exclude = parse_simple_set_op_notation(get_tag_regex(), tags)

    intersection = include & exclude
    if intersection:
        raise FilterException("Include and exclude filters intersect: {}".format(", ".join(intersection)))

    logger.debug("Tags filter include set:%s exclude set:%s", list(include), list(exclude))

    # Disable filtering
    if devtools.ya.test.const.ServiceTags.AnyTag in include:
        return lambda x: True

    # Implies ya:manual tag it isn't specified explicitly to avoid running ya:manual tests by default
    if devtools.ya.test.const.YaTestTags.Manual not in include and not list_mode:
        exclude.add(devtools.ya.test.const.YaTestTags.Manual)

    def filter_function(test_suite):
        suite_tags = set(test_suite.tags)
        if not suite_tags:
            suite_tags.add(devtools.ya.test.const.YaTestTags.Notags)

        fit = True
        if include:
            fit = suite_tags & include
        if exclude:
            fit = fit and not suite_tags & exclude
        return fit

    return filter_function


def get_project_path_filters(opts):
    res = []
    for f in opts.tests_filters:
        project_path_filter_candidate = os.path.normpath(os.path.join(opts.arc_root, f))
        if os.path.exists(project_path_filter_candidate) and os.path.isdir(project_path_filter_candidate):
            res.append(f)
    return res


def project_path_filter(project_paths):
    if project_paths:

        def filter_function(test_suite):
            return test_suite.project_path in project_paths

        return filter_function
    else:
        return lambda x: True


def filter_test_files(opts):
    files = set(opts.test_files_filter)

    def filter_function(test_suite):
        if not test_suite.is_skipped() and test_suite.fork_test_files_requested(opts):
            return test_suite.get_test_files(opts)
        if not files:
            return True
        return files & set(test_suite.get_test_files() or [])

    return filter_function


def filter_chunks(opts):
    filters = getattr(opts, 'tests_chunk_filters', [])
    if filters:
        logger.debug("Chunk filters: %s", filters)
        predicate = make_name_filter(filters)

        def filter_function(suite):
            # It's ok to generate chunks here. Filter function will be used only if chunk filtering is requested
            chunks = suite.gen_suite_chunks(opts)
            return any([x for x in chunks if predicate(x.get_name())])

        return filter_function
    else:
        return lambda x: True


def filter_unsupported_cross_compiled_tests(target_platform, skip):
    host_os = target_platform['platform']['host']['os']
    host_arch = target_platform['platform']['host']['arch']

    target_os = target_platform['platform']['target']['os']
    target_arch = target_platform['platform']['target']['arch']

    if (
        skip
        and host_os == "LINUX"
        and (target_os in ("DARWIN", "IOS") or (target_os == "LINUX" and host_arch != target_arch == "aarch64"))
    ):
        return lambda s: False
    return lambda s: True


def fixed_test_names(filters, opts):
    filter_func = make_testname_filter(filters)
    if filters:

        def filter_function(test_suite):
            tests = test_suite.get_computed_test_names(opts)
            # we need to run test node and apply filters afterwards if suite doesn't provide computable test names
            if not tests:
                return True

            return any(filter(filter_func, tests))

        return filter_function
    else:
        return lambda x: True


def filter_suites(suites: list[common_suites.AbstractTestSuite], opts, tc) -> list[common_suites.AbstractTestSuite]:
    def apply_filter(
        suites: list[common_suites.AbstractTestSuite],
        callback: Callable[[common_suites.AbstractTestSuite], bool],
        filter_description,
    ) -> list[common_suites.AbstractTestSuite]:
        logger.debug("Going to apply filter '%s' on %s suites", filter_description, len(suites))
        skipped = []
        filtered = []
        for suite in suites:
            if callback(suite):
                filtered.append(suite)
            else:
                if inspect.isfunction(filter_description):
                    reason = filter_description(suite)
                else:
                    reason = "skipped by {}".format(filter_description)
                suite.add_suite_error(reason, devtools.ya.test.const.Status.SKIPPED)
                skipped.append(common_suites.SkippedTestSuite(suite))
        if len(filtered) != len(suites):
            skipped_suites.extend(skipped)
        return filtered

    skipped_suites = []
    test_size_filters = tc.get("test_size_filters") or opts.test_size_filters
    if not test_size_filters:
        sizes = [
            devtools.ya.test.const.TestSize.Small,
            devtools.ya.test.const.TestSize.Medium,
            devtools.ya.test.const.TestSize.Large,
        ]
        test_size_filters.extend(sizes[: opts.run_tests])
    else:
        test_size_filters = [f.lower() for f in test_size_filters]

    suites = apply_filter(
        suites,
        filter_unsupported_cross_compiled_tests(tc, opts.skip_cross_compiled_tests),
        'unsupported cross-compiled tests',
    )
    suites = apply_filter(suites, lambda s: False if s.get_skipped_reason() else True, lambda s: s.get_skipped_reason())
    suites = apply_filter(suites, filter_test_size(test_size_filters), 'size')
    suites = apply_filter(
        suites, filter_suite_class_type(tc.get("test_class_filters") or opts.test_class_filters), 'class type'
    )
    suites = apply_filter(
        suites, filter_suite_type(tc.get("test_type_filters") or opts.test_type_filters), 'suite type'
    )
    suites = apply_filter(suites, tags_filter(opts.test_tags_filter, list_mode=opts.list_tests), 'tags')
    suites = apply_filter(suites, project_path_filter(opts.tests_path_filters), 'project path')
    suites = apply_filter(suites, filter_test_files(opts), 'filename filter')
    suites = apply_filter(suites, filter_chunks(opts), 'chunk filter')
    # filtering optimization - if suite can compute its test names, we can apply name-filters while building graph
    # to drop unnecessary nodes and don't build test environment (build deps, download data from sb, etc)
    suites = apply_filter(suites, fixed_test_names(opts.tests_filters, opts), 'name')
    return suites + skipped_suites
