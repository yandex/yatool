# coding: utf-8

import os
import json
import logging
import optparse
import traceback
import collections

import exts.fs
import exts.tmp

import devtools.ya.test.const
import devtools.ya.test.explore
import devtools.ya.test.filter as test_filter
import devtools.ya.test.test_types.boost_test
import devtools.ya.test.test_types.cov_test
import devtools.ya.test.test_types.fuzz_test
import devtools.ya.test.test_types.go_test
import devtools.ya.test.test_types.gtest
import devtools.ya.test.test_types.java_style
import devtools.ya.test.test_types.junit
import devtools.ya.test.test_types.library_ut
import devtools.ya.test.test_types.py_test
import devtools.ya.test.util.shared

from devtools.ya.test import facility
from devtools.ya.test import common as test_common
from devtools.ya.test.programs.test_tool.lib import testroot
from devtools.ya.test.util import tools
from yatest_lib import test_splitter

from devtools.ya.test.dependency import sandbox_storage


logger = logging.getLogger(__name__)


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--modulo", dest="modulo", default=1)
    parser.add_option("--partition-mode", dest="partition_mode", default="SEQUENTIAL")
    parser.add_option("--tests-filters", dest="tests_filters", default=[], action='append')
    parser.add_option("--test-param", dest="test_param", default=[], action='append')
    parser.add_option("--env", dest="test_env", action="append", help="Test env", default=[])
    parser.add_option("--split-by-tests", dest="split_by_tests", default=True)
    parser.add_option("--test-suite-name", dest="test_suite_name", help="name of the running test suite", default=None)
    parser.add_option(
        "--test-suite-class", dest="test_suite_class", help="class name of the running test suite", default=None
    )
    parser.add_option("--test-info-path", dest="test_info_path", help="path to test info", default=None)
    parser.add_option("--test-list-path", dest="test_list_path", help="path to test list", default=None)
    parser.add_option(
        "--test-tags", dest="test_tags", action='append', help="tags of the running test suite", default=[]
    )
    parser.add_option("--test-size", dest="test_size", help="size of the running test suite (e.g. 'fat')", default=None)
    parser.add_option(
        "--test-type", dest="test_type", help="type of the running test suite (e.g. 'pytest')", default=None
    )
    parser.add_option("--project-path", dest="project_path", help="project path arcadia root related")
    parser.add_option("--source-root", dest="source_root", help="source route", action='store')
    parser.add_option("--build-root", dest="build_root", help="build route", action='store')
    parser.add_option("--target-platform-descriptor", dest="target_platform_descriptor")
    parser.add_option(
        "--multi-target-platform-run", dest="multi_target_platform_run", action='store_true', default=False
    )
    parser.add_option("--is-skipped", dest="is_skipped", action='store_true', default=False)
    parser.add_option("--test-name", dest="computed_test_name", default=[], action='append')
    parser.add_option(
        "--sandbox-resources-root", dest="sandbox_resources_root", help="sandbox resources root", action='store'
    )
    parser.add_option(
        "--test-related-path",
        dest="test_related_paths",
        help="list of paths requested by wrapper (suite) or test - these paths will form PYTHONPATH",
        action='append',
        default=[],
    )
    parser.add_option(
        "--sandbox-resource",
        dest="sandbox_resources",
        help="list of sandbox resources that test is depend on",
        action="append",
        default=[],
    )
    parser.add_option("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_option(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_option("--no-clean-environment", dest="create_clean_environment", action='store_false', default=True)
    options, cmd = parser.parse_args()

    return options, cmd


def main():
    options, list_cmd = get_options()
    test_context = {"runtime": {"project_path": options.project_path}}
    if options.test_type == "exectest" and options.test_param:
        test_params = dict(x.split("=", 1) for x in options.test_param)
        test_context["runtime"]["test_params"] = test_params
    context_path = os.path.join(options.build_root, devtools.ya.test.const.SUITE_CONTEXT_FILE_NAME)
    with open(context_path, 'w') as afile:
        json.dump(test_context, afile)
    os.environ["YA_TEST_CONTEXT_FILE"] = context_path

    cwd = os.getcwd()
    devtools.ya.test.util.shared.setup_logging(options.log_level, options.log_path)

    suite_classes = list(devtools.ya.test.explore.SUITE_MAP.values())

    # XXX
    suite_classes.append(devtools.ya.test.test_types.common.DiffTestSuite)

    suite_class = None
    for klass in suite_classes:
        if options.test_suite_class == klass.__name__:
            suite_class = klass
            break

    if not suite_class:
        raise ValueError("Unsupported test type: %s", options.test_suite_class)

    # create an isolated environment
    source_root = options.source_root
    build_root = options.build_root
    sandbox_resources = options.sandbox_resources

    resources_root = build_root
    # cached storage is needed only for environment creation as all resources should come via graph
    cached_storage = sandbox_storage.SandboxStorage(resources_root, use_cached_only=True, update_last_usage=False)

    work_dir = test_common.get_test_suite_work_dir(
        build_root,
        options.project_path,
        options.test_suite_name,
        target_platform_descriptor=options.target_platform_descriptor,
        multi_target_platform_run=options.multi_target_platform_run,
    )

    out_dir = os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME)
    exts.fs.create_dirs(out_dir)

    if options.create_clean_environment:
        new_source_root, new_build_root = testroot.create_environment(
            options.test_related_paths,
            source_root,
            build_root,
            cwd,
            testroot.EnvDataMode.Symlinks,  # wait no copy data to environment during listing
        )
    else:
        new_source_root = source_root

    testroot.prepare_work_dir(build_root, work_dir, sandbox_resources, cached_storage)

    if options.computed_test_name:
        test_cases = [facility.TestCase(testname, None) for testname in options.computed_test_name]
        dump_tests(
            options,
            [
                {
                    "test": tc.get_class_name(),
                    "subtest": tc.get_test_case_name(),
                    "skipped": False,
                    "tags": options.test_tags,
                }
                for tc in test_cases
            ],
            None,
        )
        return

    list_cmd = devtools.ya.test.util.shared.change_cmd_root(list_cmd, source_root, new_source_root, build_root)

    env = os.environ.copy()
    env.update(entry.split("=", 1) for entry in options.test_env)
    # change roots in the env's PYTHONPATH
    python_paths = devtools.ya.test.util.shared.change_cmd_root(
        options.test_related_paths, source_root, new_source_root, build_root
    )
    python_dirs = set()
    for p in python_paths:
        if os.path.isfile(p):
            python_dirs.add(os.path.dirname(p))
        else:
            python_dirs.add(p)
    tools.append_python_paths(env, python_dirs, overwrite=True)
    with exts.tmp.environment(env):
        try:
            if options.is_skipped:
                tests = []
            else:
                tests = suite_class.list(list_cmd, work_dir)
            error = None
        except Exception:
            tests = []
            error = traceback.format_exc()

    if options.test_list_path:
        tests_for_list = [t.test + '::' + t.subtest for t in tests]
        logger.debug("tests before filter: %s", tests_for_list)
        logger.debug("filters: %s", options.tests_filters)
        if options.tests_filters:
            tests_for_list = list(filter(test_filter.make_testname_filter(options.tests_filters), tests_for_list))
        logger.debug("tests after filter: {}".format(tests_for_list))

        test_classes = collections.defaultdict(list)
        for t in tests_for_list:
            test_class, test_name = t.rsplit('::', 1)
            test_classes[test_class].append(test_name)
        tests_chunks = []
        modulo = int(options.modulo)
        for i in range(modulo):
            chunk = test_splitter.filter_tests_by_modulo(
                test_classes, modulo, i, bool(options.split_by_tests), options.partition_mode
            )
            tests_chunks.append([])
            for cls, tsts in chunk.items():
                tests_chunks[i].extend(cls + '::' + t for t in tsts)

        logger.debug("tests after chunking: {}".format(tests_chunks))

        with open(options.test_list_path, 'w') as afile:
            json.dump(tests_chunks, afile)
    else:
        dump_tests(options, [t.to_json() for t in tests], error)


def dump_tests(options, tests, error=None):
    with open(options.test_info_path, "w") as res_file:
        json.dump(
            {
                "project-path": options.project_path,
                "tests": tests,
                "test-size": options.test_size,
                "test-tags": options.test_tags,
                "test-type": options.test_type,
                "target-platform-descriptor": options.target_platform_descriptor,
                "error": error,
            },
            res_file,
            ensure_ascii=False,
        )


if __name__ == '__main__':
    main()
