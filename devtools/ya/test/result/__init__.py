# coding: utf-8

import json
import logging
import os
import traceback

import core.error
import exts.archive
import exts.func
import exts.fs
import exts.windows
import devtools.ya.test.common as test_common
import devtools.ya.test.const
from yalibrary import platform_matcher
from library.python import strings

import app_config

if app_config.in_house:
    from devtools.distbuild.libs.node_status.python import node_status

logger = logging.getLogger(__name__)


class NodeStatusConvertionError(Exception):
    pass


class BaseTestSuiteRunResult(object):
    def __init__(self):
        self.tests = []

    @property
    def exit_code(self):
        raise NotImplementedError

    @property
    def stdout(self):
        raise NotImplementedError

    @property
    def stderr(self):
        raise NotImplementedError

    @property
    def elapsed(self):
        raise NotImplementedError

    @property
    def test_timeout(self):
        raise NotImplementedError

    @property
    def flaky(self):
        return False

    @property
    def test_size(self):
        raise NotImplementedError

    @property
    def target_platform_descriptor(self):
        raise NotImplementedError

    @exts.func.lazy_property
    def trace_report_path(self):
        raise NotImplementedError


class TestPackedResultView(BaseTestSuiteRunResult):
    def __init__(self, out_dir):
        super(TestPackedResultView, self).__init__()
        self._output_dir = out_dir

    @exts.func.lazy_property
    def exit_code(self):
        return self._meta['exit_code']

    @exts.func.lazy_property
    def elapsed(self):
        return self._meta['elapsed']

    @exts.func.lazy_property
    def test_timeout(self):
        return self._meta['test_timeout']

    @exts.func.lazy_property
    def trace_report_path(self):
        return self._output_path(devtools.ya.test.const.TRACE_FILE_NAME)

    @exts.func.lazy_property
    def stderr(self):
        return exts.fs.read_file_unicode(self._output_path('stderr'), binary=True)

    @exts.func.lazy_property
    def stdout(self):
        return exts.fs.read_file_unicode(self._output_path('stdout'), binary=True)

    @exts.func.lazy_property
    def suite_name(self):
        return self._meta['name']

    @exts.func.lazy_property
    def suite_project_path(self):
        return self._meta['project']

    @exts.func.lazy_property
    def test_size(self):
        return self._meta.get('test_size', devtools.ya.test.const.TestSize.Small)

    @exts.func.lazy_property
    def test_tags(self):
        return self._meta.get('test_tags', [])

    @exts.func.lazy_property
    def target_platform_descriptor(self):
        return self._meta.get("target_platform_descriptor", "")

    @exts.func.lazy_property
    def multi_target_platform_run(self):
        return self._meta.get("multi_target_platform_run", False)

    @exts.func.lazy_property
    def suite_type(self):
        return self._meta['test_type']

    @exts.func.lazy_property
    def suite_ci_type(self):
        return self._meta['test_ci_type']

    @exts.func.lazy_property
    def uid(self):
        return self._meta['uid']

    def _output_path(self, name):
        return os.path.join(self._output_dir, name)

    def get_output_dir(self):
        return self._output_dir

    @exts.func.lazy_property
    def _meta(self):
        with open(self._output_path('meta.json')) as fp:
            return test_common.strings_to_utf8(json.load(fp=fp))


def load_suite_from_output(output, resolver):
    return load_suite_from_result(TestPackedResultView(output), output, resolver)


def load_suite_from_result(result, output, resolver):
    import devtools.ya.test.test_types.common

    suite = devtools.ya.test.test_types.common.PerformedTestSuite(
        result.suite_name,
        result.suite_project_path,
        size=result.test_size,
        tags=result.test_tags,
        target_platform_descriptor=result.target_platform_descriptor,
        suite_type=result.suite_type,
        suite_ci_type=result.suite_ci_type,
        multi_target_platform_run=result.multi_target_platform_run,
        uid=result.uid,
    )
    suite.set_work_dir(output)
    suite.load_run_results(result.trace_report_path, resolver)
    return suite


def convert_distbuild_to_test_status(status_node, strict=False):
    error_convention = {
        node_status.EResultStatus.RS_TIMEOUT: devtools.ya.test.const.Status.TIMEOUT,
        node_status.EResultStatus.RS_CANCELED: devtools.ya.test.const.Status.INTERNAL,
        node_status.EResultStatus.RS_SIZE_LIMIT_EXCEEDED: devtools.ya.test.const.Status.FAIL,
        node_status.EResultStatus.RS_INCORRECT_OUTPUT: devtools.ya.test.const.Status.INTERNAL,
        node_status.EResultStatus.RS_RAM_REQUIREMENTS_EXCEEDED: devtools.ya.test.const.Status.FAIL,
        node_status.EResultStatus.RS_INFRASTRUCTURE_ERROR: devtools.ya.test.const.Status.INTERNAL,
        node_status.EResultStatus.RS_FAIL: devtools.ya.test.const.Status.INTERNAL,
    }

    if status_node in error_convention:
        return error_convention[status_node]
    elif strict:
        raise NodeStatusConvertionError()
    else:
        return devtools.ya.test.const.Status.INTERNAL


def fill_suites_results(suites, builder, results_root, resolver=None):
    if not suites:
        return

    def get_errors(target):
        errs = []

        # search for errors in broken dependencies
        if target in builder.build_result.failed_deps:
            failed_deps = builder.build_result.failed_deps[target]

            for failed_dep in failed_deps:
                if failed_dep in builder.build_result.build_errors:
                    project_path, platform, uid = failed_dep

                    errs.append("Broken target [[imp]]{}[[rst]]:".format(project_path))
                    errs.append("\n".join(builder.build_result.build_errors[failed_dep]))
        # specified target is broken directly, there are no broken deps
        else:
            errs.append("\n".join(builder.build_result.build_errors[target]))
        return errs

    build_errors_map = {uid: (path, platform, uid) for path, platform, uid in builder.build_result.build_errors}

    def collect_errors_for_suite(suite):
        errs = []

        for dep_uid in suite.get_build_dep_uids():
            if dep_uid in build_errors_map:
                suite_key = build_errors_map[dep_uid]
                project_path, _, _ = suite_key
                errs.append("[[imp]]{}[[rst]]".format(project_path))
                errs += get_errors(suite_key)

        return errs

    def build_broken_deps_report(uid, node_errors, limit=4):
        if not node_errors:
            return (
                devtools.ya.test.const.Status.INTERNAL,
                "Infrastructure error - contact devtools@ for details. Node {} treated to be broken, but has no broken deps".format(
                    uid
                ),
            )

        statuses = list(filter(None, [builder.build_result.node_status_map.get(u) for u, _ in node_errors]))
        if statuses:
            # Status code are provided by distbuild
            derived_status = node_status.derive_status(statuses)
            test_status = convert_distbuild_to_test_status(derived_status)
            exit_code = None
        else:
            derived_status = None
            # Exit codes are provided by local runner
            exit_codes = [1] + [builder.build_result.exit_code_map.get(u, 1) for u, _ in node_errors]
            exit_code = core.error.merge_exit_codes(exit_codes)
            if exit_code == devtools.ya.test.const.TestRunExitCode.InfrastructureError:
                test_status = devtools.ya.test.const.Status.INTERNAL
            else:
                test_status = devtools.ya.test.const.Status.FAIL

        # Sandbox resource fetching node has retries and failure in most cases
        # is caused by a missing resource, which should be treated as
        # regular fail - it's user side problem.
        # XXX waiting for YA-433
        if all(u.startswith('sandbox-resource') for u, _ in node_errors):
            test_status = devtools.ya.test.const.Status.FAIL

        lines = [
            "Node {} wasn't executed due to broken {}".format(
                uid, "{} dependencies".format(len(node_errors)) if len(node_errors) > 1 else "dependency"
            ),
        ]

        if derived_status is not None:
            lines += [
                "Node's derived status: {}. {}".format(
                    derived_status, node_status.get_human_readable_message(derived_status)
                ),
            ]
        elif exit_code is not None:
            lines += ["Node's derived exit code: {}".format(exit_code)]

        # XXX
        header_approximate_size = 300
        node_snippet_limit = (
            devtools.ya.test.const.CONSOLE_SNIPPET_LIMIT // min(len(node_errors), limit)
        ) - header_approximate_size

        for uid, err_msg in node_errors[:limit]:
            st = builder.build_result.node_status_map.get(uid)
            if app_config.in_house and st:
                error_reason = node_status.get_human_readable_message(st)
            else:
                error_reason = "failed"
            lines += ["[[imp]]* {}[[rst]]: {}".format(uid, error_reason)]

            if uid in builder.build_result.node_build_errors_links:
                logs = [x for u, urls in builder.build_result.node_build_errors_links[uid] for x in urls]
                ellipsis_text = "\n..[truncated] Full logs: [[warn]]{}[[rst]]".format(logs)
            else:
                ellipsis_text = "\n..[truncated]"
            lines += [strings.truncate(err_msg, node_snippet_limit, msg=ellipsis_text)]

        if len(node_errors) > limit:
            lines += ["{} more node errors are omitted.".format(len(node_errors) - limit)]

        return test_status, "\n".join(x.strip() for x in lines)

    ok_tests = builder.build_result.ok_nodes

    for suite in suites:
        work_dir = test_common.get_test_suite_work_dir(
            results_root,
            suite.project_path,
            suite.name,
            target_platform_descriptor=suite.target_platform_descriptor,
            multi_target_platform_run=suite.multi_target_platform_run,
        )
        suite.set_work_dir(work_dir)

        uid = suite.uid
        if uid in ok_tests:
            # XXX suite has already been loaded
            if suite.chunks:
                continue

            result = TestPackedResultView(work_dir)
            try:
                suite.load_run_results(result.trace_report_path, resolver)
            except Exception:
                msg = "Infrastructure error - contact devtools@.\nFailed to fill suite results:{}\n".format(
                    traceback.format_exc()
                )
                suite.add_suite_error(msg, devtools.ya.test.const.Status.INTERNAL)
                logging.debug(msg)
        else:
            errs = collect_errors_for_suite(suite)
            if errs:
                suite.add_suite_error(
                    "Depends on broken: {}".format("\n".join(errs)), devtools.ya.test.const.Status.MISSING
                )
            elif uid in builder.build_result.node_build_errors:
                # TODO
                # Successful chunk run and tests within it should be fully reported,
                # broken chunks should be reported separately as well.
                # For more info see https://st.yandex-team.ru/YA-417
                node_errors = builder.build_result.node_build_errors[uid]
                status_node = builder.build_result.node_status_map.get(uid)

                # Node might be broken and still not presented in the node_status_map.
                # It can happen if node wasn't executed due broken dependencies.
                # Most common case for broken test suite is broken test chunk or broken sandbox resource downloading node.
                if status_node is None:
                    status, msg = build_broken_deps_report(uid, node_errors)
                else:
                    status = convert_distbuild_to_test_status(status_node)
                    msg = (
                        node_status.get_human_readable_message(status_node)
                        + '\n'
                        + '\n'.join('Node {} failed: {}'.format(d, m) for d, m in node_errors)
                    )

                logger.warning("%s (uid=%s): %s", suite, suite.uid, msg)
                suite.add_suite_error(msg, status)
            else:
                target_platform = suite.target_platform_descriptor or platform_matcher.my_platform()
                key = (suite.project_path, target_platform, suite.uid)
                node_errors = builder.build_result.build_errors.get(key)

                deps_mgs = ', '.join('[{} {}]'.format(u, d) for u, d in suite.get_build_deps())
                msg = "Infrastructure error - contact devtools@ for details. Suite build deps: {}".format(deps_mgs)
                if node_errors:
                    msg += "\n{}".format("\n".join(node_errors))
                logger.warning("%s (uid=%s): %s", suite, suite.uid, msg)
                suite.add_suite_error(msg, devtools.ya.test.const.Status.INTERNAL)

            if resolver:
                suite.fix_roots(resolver)
