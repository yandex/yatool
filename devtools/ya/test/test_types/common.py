# coding: utf-8

import collections
import io
import itertools
import json
import logging
import os
import sortedcontainers
import sys
import time

import six

import exts.func
import exts.fs
import exts.path2
import exts.log
import exts.windows
import exts.tmp
import exts.hashing as hashing
import devtools.ya.test.util.shared
from devtools.ya.test import facility
from devtools.ya.test import tracefile
from devtools.ya.test.dependency import external_tools
from devtools.ya.test import common as test_common
from devtools.ya.test.system import process
from yatest_lib import external
import devtools.ya.test.const
import devtools.ya.test.util.tools
import build.plugins.lib._metric_resolvers as mr


ATTRS_TO_STATE_HASH = [
    '_modulo',
    'project_path',
    'global_resources',
    'recipes',
    'requirements',
    'timeout',
    'test_size',
    'name',
]
DIFF_TEST_TYPE = "canon_diff"

yatest_logger = logging.getLogger(__name__)


class AbstractTestSuite(facility.Suite):
    """
    The top class in the test hierarchy, all test types should inherit from it
    """

    def get_type(self):
        """
        Returns human readable suite type name
        """
        raise NotImplementedError()

    def _get_meta_info_parser(self):
        """
        Return parser for meta information from which test suites are generated
        """
        return facility.DartInfo

    @property
    def class_type(self):
        """
        Type of the suite (style, regular, etc)
        """
        return devtools.ya.test.const.SuiteClassType.UNCLASSIFIED

    @classmethod
    def get_ci_type_name(cls):
        return "test"

    def get_sandbox_uid_extension(self):
        """
        This works as a salt to change uid for resource validation tests
        """
        return ""

    def get_resource_tools(self):
        """
        Returns list of names of custom tools required to run suite
        """
        return []

    def __init__(
        self,
        meta_dict,
        modulo=1,
        modulo_index=0,
        target_platform_descriptor=None,
        split_file_name=None,
        multi_target_platform_run=False,
    ):
        """
        :param meta: meta info like parsed `test.dart` file
        """
        super(AbstractTestSuite, self).__init__()
        self.meta = self._get_meta_info_parser()(meta_dict)
        self._result_uids = []
        self._output_uids = []
        self.dep_uids = []
        self.preresults = []
        self.flaky = False
        self.uid = None
        self.save_old_canondata = False
        self._test_data_map = None
        self._requirements_map = None
        self._pretest_uid = None

        # TODO only PerformedTestSuite may have empty dart. remove 'if' after removing inheritance
        if self.meta.source_folder_path is not None:
            assert not self.meta.source_folder_path.startswith("/"), 'Source folder path must be relative ({})'.format(
                self.meta.source_folder_path
            )
        self.symlinks_dir = None  # TODO: only for fleur tests, fix and remove
        self._work_dir = None
        self._specified_timeout = None
        self.run_only_subtests = None
        self._additional_filters = []
        self._modulo = modulo
        self._modulo_index = modulo_index
        self._split_file_name = split_file_name

        self._target_platform_descriptor = target_platform_descriptor
        self._multi_target_platform_run = multi_target_platform_run
        self._build_deps = sortedcontainers.SortedDict()
        self._dependency_errors = []
        # MDS resource dependencies
        self.mds_resource_deps = []

        self.wine_path = None

        self.global_resources = self.meta.global_resources.copy()

        self.special_runner = ''
        if devtools.ya.test.const.YaTestTags.YtRunner in self.tags:
            self.special_runner = 'yt'
        elif devtools.ya.test.const.YaTestTags.ForceSandbox in self.tags:
            self.special_runner = 'sandbox'

        self._links_map = {}

    def need_wine(self):
        return self.wine_path is not None

    # XXX Deprecated, see YA-1440
    def init_from_opts(self, opts):
        pass

    def get_classpath(self):
        return None

    def save(self):
        #  needed for ya dump
        return {
            'dart_info': self.meta.meta_dict,
            'tests': self.tests,
            'result_uids': self._result_uids,
            'output_uids': self._output_uids,
            'flaky': self.flaky,
            'uid': self.uid,
            'build_deps': dict(self._build_deps),
            'run_only_subtests': self.run_only_subtests,
            'modulo': self._modulo,
            'modulo_index': self._modulo_index,
            'target_platform_descriptor': self._target_platform_descriptor,
            'multi_target_platform_run': self._multi_target_platform_run,
            'split_file_name': self._split_file_name,
            'special_runner': self.special_runner,
        }

    def support_splitting(self, opts=None):
        """
        Does test suite support splitting
        """
        return False

    def support_recipes(self):
        """
        Does test suite support recipes
        """
        return True

    def support_list_node(self):
        """
        Does test suite support list_node before test run
        """
        return False

    def support_retries(self):
        """
        Does test suite support retries
        """
        raise NotImplementedError()

    @property
    def smooth_shutdown_signals(self):
        """
        Instead of immediate killing, wrapper will receive a specified signals
        and get some extra time to smoothly shutdown test, when running out of timeout
        """
        return []

    @classmethod
    def list(cls, cmd, cwd):
        """
        Runs list command
        :param cmd: list command
        :param cwd: work dir
        :return: list of SubtestInfo instances
        """
        raise NotImplementedError

    @exts.func.memoize()
    def get_suite_id(self):
        """
        Should be used to distinguish suites
        :return:
        """
        return "{}-{}-{}".format(self.project_path, self.get_type(), self.get_ci_type_name())

    def insert_additional_filters(self, filters):
        """
        Insert filters in addition to ones provided in opts explicitly
        E.g. filter only those that failed in the last attempt
        """
        self._additional_filters.extend(filters)

    def get_additional_filters(self):
        """
        Get additional filters
        """
        return self._additional_filters

    @property
    def result_uids(self):
        """
        :return: list of nodes with test's meta results
            may not contain nodes with testing_output_stuff.tar output file
        """
        return list(self._result_uids)

    @property
    def output_uids(self):
        """
        :return: list of nodes with all test's output data including results.
            contains nodes with testing_output_stuff.tar output file
        """
        return sorted(set(self._result_uids + self._output_uids))

    @property
    def name(self):
        """
        Test name
        """
        val = self.meta.test_name
        if val == ".":
            val = os.path.basename(self._source_folder_path(''))
        return val

    @property
    def target_platform_descriptor(self):
        """
        Definitive description of the target platform with tags
        """
        return self._target_platform_descriptor or "unknown-target-platform"  # To fix ujson dump in PY3

    @property
    def multi_target_platform_run(self):
        """
        Whether we need to build for multiple target platforms
        """
        return self._multi_target_platform_run

    def binary_path(self, root):
        if self.meta.binary_path == 'python':
            binary = 'python'  # hack to make USE_ARCADIA_PYTHON=no work
        else:
            # is binary_path always present here?
            binary = os.path.join(root, self.meta.binary_path)
        if exts.windows.on_win():
            if not binary.endswith('.exe'):
                yatest_logger.debug('Missed binary .exe suffix')
                binary += '.exe'
        return binary

    @property
    def _use_arcadia_python(self):
        """
        Use python from contrib/
        'no' means use python installed on your system
        """
        return (self.meta.use_arcadia_python or 'yes') == 'yes'

    def _source_folder_path(self, root):
        return os.path.join(root, self.meta.source_folder_path)

    @property
    def declared_timeout(self):
        """
        :return: declared timeout in ya.make
        """
        timeout = self.meta.test_timeout
        if timeout is not None and isinstance(timeout, six.string_types):
            timeout = timeout.strip()
        if timeout and int(timeout):
            return int(timeout)
        return devtools.ya.test.const.TestSize.get_default_timeout(self.test_size)

    @property
    def timeout(self):
        """
        :return: timeout allocated for the suite
        """
        if self._specified_timeout:
            return self._specified_timeout
        return self.declared_timeout

    def set_timeout(self, value):
        """
        Set refined timeout calculated based on various properties:
        Passed parameters, suite type etc
        """
        self._specified_timeout = value

    def _tested_file_rel_path(self):
        return os.path.join(self.meta.build_folder_path, self.test_project_filename)

    @property
    def test_project_filename(self):
        """
        Name of the produced binary
        """
        return self.meta.tested_project_filename or self.meta.tested_project_name

    @property
    def project_path(self):
        """
        Path from arcadia root to the test module
        """
        return self.meta.source_folder_path

    @property
    def test_size(self):
        tsize = self.meta.size or devtools.ya.test.const.TestSize.Small
        return tsize.lower()

    @property
    def _custom_dependencies(self):
        """
        List of custom dependencies passed from plugins
        """
        return list(
            set(
                [x for x in self.meta.custom_dependencies.split(' ') if x and not x == "$TEST_DEPENDS_VALUE"]
                + [self.project_path]
            )
        )

    @property
    def recipes(self):
        """
        Base64 encoded content of all related USE_RECIPE separated by new line
        """
        if self.support_recipes():
            return self.meta.test_recipes
        else:
            return None

    @property
    def env(self):
        """
        List of env variables set with ENV macros
        """
        return self.meta.test_env

    @property
    def setup_pythonpath_env(self):
        return False

    @property
    def yt_spec_files(self):
        """
        List of json files with specification of tests that will be executed in YT
        """
        return self.meta.yt_spec

    @property
    def requires_test_data(self):
        return True

    def _get_all_test_data(self, data_type):
        """
        Content of DATA and DATA_FILES
        """
        if not self.requires_test_data:
            return []

        types = (
            ('sbr', 'sbr', False),
            ('mds', 'mds', False),
            ('arcadia/', 'arcadia', True),
            ('arcadia_tests_data/', 'atd', True),
            ('ext', 'ext', False),
        )

        if self._test_data_map is None:
            self._test_data_map = collections.defaultdict(set)

            for entry in [_f for _f in self.meta.test_data if _f]:
                for prefix, dtype, remove_prefix in types:
                    if entry.startswith(prefix):
                        if remove_prefix:
                            x = entry[len(prefix) :]
                        else:
                            x = entry
                        self._test_data_map[dtype].add(x)

            for _, dtype, _ in types:
                self._test_data_map[dtype] = sorted(self._test_data_map[dtype])

        return self._test_data_map[data_type]

    def get_test_docker_images(self):
        """
        DOCKER_IMAGE (
            <link1>=TAG1
            <link2>=TAG2
        )
        return value:  [(<link1>, TAG1), (<link2>, TAG2), ...]
        """
        return [x.rsplit('=', 1) for x in self.meta.docker_images]

    @property
    def python_paths(self):
        return [_f for _f in self.meta.python_paths if _f]

    @property
    def test_run_cwd(self):
        # Cwd can only be used only when test data is required
        # Otherwise, there might be no target directory
        if self.requires_test_data:
            cwd = self.meta.test_cwd
            if cwd:
                cwd = "$(SOURCE_ROOT)/" + cwd
            return cwd

    @property
    def links_map(self):
        return self._links_map

    def update_links_map(self, kwargs):
        # type: (dict[str, str]) -> None
        self._links_map.update(kwargs)

    def get_sandbox_resources(self):
        return self._get_all_test_data(data_type='sbr')

    def get_mds_resources(self):
        return self._get_all_test_data(data_type='mds')

    def get_ext_resources(self):
        return self._get_all_test_data(data_type='ext')

    def data_root(self, root):  # TODO: remove it
        return os.path.normpath(os.path.join(root, "..", "arcadia_tests_data"))

    def get_arcadia_test_data(self):
        return self._get_all_test_data(data_type='arcadia')

    def get_atd_data(self):
        return self._get_all_test_data(data_type='atd')

    def get_state_hash(self):
        """
        This is used to pinpoint exactly the same tests between different runs
        """
        res = ''
        for attr in ATTRS_TO_STATE_HASH:
            res += str(getattr(self, attr, ''))
        return hashing.md5_value(res)

    def stdout_path(self):
        return os.path.join(self.output_dir(), "run.stdout")

    def messages_path(self):
        return os.path.join(self.output_dir(), "run.messages")

    def stderr_path(self):
        return os.path.join(self.output_dir(), "stderr")

    def list_stderr_path(self):
        return os.path.join(self.output_dir(), "list_stderr")

    def get_test_dependencies(self):
        """
        Get all test dependencies
        """
        deps = set()
        deps.add(os.path.dirname(self._tested_file_rel_path()).strip('/'))
        deps.add(os.path.dirname(self.binary_path('')).strip('/'))
        deps.update(self._custom_dependencies)
        return [_f for _f in deps if _f]

    def get_test_related_paths(self, arc_root, opts):
        return [os.path.join(arc_root, f) for f in self.yt_spec_files]

    def _script_path(self, root):
        """
        Path to the script file that runs tests
        """
        raise NotImplementedError()

    def __str__(self):
        return "Test [project=%s, name=%s]" % (self.project_path, self.name)

    def __repr__(self):
        return str(self)

    # XXX rename
    def work_dir(self, *path):
        if self._work_dir is None:
            self._work_dir = test_common.get_test_suite_work_dir(
                "$(BUILD_ROOT)",
                self.project_path,
                self.name,
                target_platform_descriptor=self.target_platform_descriptor,
                multi_target_platform_run=self.multi_target_platform_run,
            )

        if path:
            return self._work_dir + "/" + "/".join(path)
        return self._work_dir

    def set_work_dir(self, work_dir):
        self._work_dir = work_dir

    def output_dir(self, *path):
        """
        Path to test outputs (logs etc)
        """
        return os.path.join(self.work_dir(), devtools.ya.test.const.TESTING_OUT_DIR_NAME, *path)

    def get_list_cmd(self, arc_root, build_root, opts):
        """
        Gets command to get list of subtests. This command will be given to cls.list
        :param arc_root: source root
        :param build_root: build root
        :param opts: current run options
        :return: list of command arguments
        """
        raise NotImplementedError

    def get_list_cmd_inputs(self, opts):
        """
        :return: get_list_cmd inputs list
        """
        return []

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        """
        Get the command to run the test during the distributed build
        :param opts: current run options
        :return:
        """
        raise NotImplementedError()

    def get_run_cmd_inputs(self, opts):
        """
        :return: run_cmd inputs list
        """
        return [os.path.join('$(SOURCE_ROOT)', self.project_path)]

    def get_prepare_test_cmds(self):
        """
        Specifies extra cmds and inputs for test node
        """
        return [], []

    def has_prepare_test_cmds(self):
        return False

    def setup_environment(self, env, opts):
        """
        setup environment for running test command
        """
        env["YA_PYTHON_BIN"] = self.get_python_bin(opts)
        python_lib = self.get_python_library(opts)
        if python_lib:
            env["YA_PYTHON_LIB"] = python_lib

    def load_run_results(self, trace_file_path, resolver=None, relaxed=True):
        if self.is_skipped():
            return

        if trace_file_path and os.path.exists(trace_file_path):
            tracefile.TestTraceParser.parse_from_file(trace_file_path, suite=self, relaxed=relaxed)

        if resolver:
            self.fix_roots(resolver)

    def generate_trace_file(self, trace_file_path, append=False):
        mode = "a" if append else "w"
        with io.open(trace_file_path, mode, encoding='utf8') as trace_file:

            def trace(name, value):
                event = {'timestamp': time.time(), 'value': value, 'name': name}
                try:
                    trace_file.write(six.text_type(json.dumps(event) + '\n'))
                except UnicodeDecodeError:
                    yatest_logger.error("Decode error with event: %s", event)
                    raise

            suite_event = {}
            if self._errors:
                suite_event['errors'] = [
                    (devtools.ya.test.const.Status.TO_STR[status], test_common.to_utf8(msg))
                    for status, msg in self._errors
                ]
            if self.logs:
                suite_event['logs'] = self.logs
            if self.metrics:
                suite_event['metrics'] = self.metrics

            if suite_event:
                trace('suite-event', suite_event)

            for chunk in self.chunks:
                chunk_event = {}
                if chunk.logs:
                    chunk_event['logs'] = chunk.logs
                if chunk.metrics:
                    chunk_event['metrics'] = chunk.metrics
                if chunk._errors:
                    chunk_event['errors'] = [
                        (devtools.ya.test.const.Status.TO_STR[status], test_common.to_utf8(msg))
                        for status, msg in chunk._errors
                    ]

                if chunk_event:
                    if chunk.filename:
                        chunk_event['chunk_filename'] = chunk.filename
                    if chunk.nchunks:
                        chunk_event['nchunks'] = chunk.nchunks
                        chunk_event['chunk_index'] = chunk.chunk_index

                    trace('chunk-event', chunk_event)

                for test_case in chunk.tests:
                    # XXX backward compatibility - remove after DEVTOOLS-2447
                    class_name, subtest_name = test_case.name.rsplit("::", 1)
                    # no need to report test-started event, since we know the final status of the test case
                    message = {
                        'class': class_name,
                        'cwd': self.work_dir(),
                        'status': devtools.ya.test.const.Status.TO_STR[test_case.status],
                        'subtest': subtest_name,
                        'time': test_case.elapsed,
                    }
                    comment = test_common.to_utf8(test_case.comment)
                    if comment:
                        message['comment'] = comment
                    if test_case.test_type:
                        message['type'] = test_case.test_type
                    if chunk.filename:
                        message['chunk_filename'] = chunk.filename
                    if chunk.nchunks:
                        message['nchunks'] = chunk.nchunks
                        message['chunk_index'] = chunk.chunk_index
                    if test_case.is_diff_test:
                        message['is_diff_test'] = True
                    if test_case.logs:
                        message['logs'] = test_case.logs
                    if test_case.metrics:
                        message['metrics'] = test_case.metrics
                    if test_case.path:
                        message['path'] = test_case.path
                    if test_case.result is not None:
                        message['result'] = test_case.result
                    if test_case.tags:
                        message['tags'] = test_case.tags
                    trace('subtest-finished', message)

    def add_suite_error(self, msg, status=devtools.ya.test.const.Status.FAIL):
        self.add_error(msg, status)

    def add_suite_info(self, msg):
        self.add_info(msg)

    def add_chunk_error(self, msg, status=devtools.ya.test.const.Status.FAIL):
        self.chunk.add_error(msg, status)

    def add_chunk_info(self, msg):
        self.chunk.add_info(msg)

    def gen_suite_chunks(self, opts):
        split_test_factor = self.get_split_factor(opts) if self.support_splitting(opts) else 1
        if self.fork_test_files_requested(opts):
            test_files = self.get_test_files(opts) or [None]
        else:
            test_files = [None]

        chunks = []
        for test_file, index in itertools.product(test_files, range(split_test_factor)):
            chunks.append(facility.Chunk(nchunks=split_test_factor, chunk_index=index, filename=test_file))
        assert chunks
        return chunks

    def is_skipped(self):
        return not self.tests and len(self._errors) == 1 and self._errors[0][0] == devtools.ya.test.const.Status.SKIPPED

    def get_status(self, relaxed=False):
        # the suite was skipped for some reason in errors
        if self.is_skipped():
            return devtools.ya.test.const.Status.SKIPPED
        return super(AbstractTestSuite, self).get_status(relaxed)

    def fix_roots(self, resolver):
        def resolve_external_path(value, _):
            if external.is_external(value):
                value["uri"] = resolver.substitute(value["uri"])
            return value

        self._errors = [(status, resolver.substitute(comment)) for status, comment in self._errors]
        self.logs = {k: resolver.substitute(v) for k, v in self.logs.items()}
        self._work_dir = resolver.substitute(self._work_dir)

        for chunk in self.chunks:
            chunk.logs = {k: resolver.substitute(v) for k, v in chunk.logs.items()}
            chunk._errors = [(status, resolver.substitute(comment)) for status, comment in chunk._errors]

            for test_case in chunk.tests:
                external.apply(resolve_external_path, test_case.result)
                test_case.comment = resolver.substitute(test_case.comment)
                test_case.logs = {k: resolver.substitute(v) for k, v in test_case.logs.items()}

    def tests_by_status(self, status):
        return len([test for chunk in self.chunks for test in chunk.tests if test.status == status])

    def setup_dependencies(self, graph, **kwargs):
        self.fill_test_build_deps(graph)

    def get_dependency_errors(self):
        return self._dependency_errors

    def fill_test_build_deps(self, graph):
        for suite_dep in self.get_test_dependencies():
            found = False
            for uid in graph.get_project_uids(suite_dep):
                found = True
                project, toolchain, _, _, tags = graph.get_projects_by_uids(uid)
                self.add_build_dep(project, toolchain, uid, tags)
            for uid in graph.get_uids_by_outputs(suite_dep):
                found = True
                module = graph.get_projects_by_uids(uid)
                if module:
                    project, toolchain, _, _, tags = module
                    self.add_build_dep(project, toolchain, uid, tags)
                else:
                    # Always add build_dep to avoid missing deps
                    node = graph.get_node_by_uid(uid)
                    self.add_build_dep(node['outputs'][-1], 'unspecified', uid, [])

            if not found:
                msg = "Cannot resolve dependency '{}' for test '{} ({})'".format(
                    suite_dep, self.project_path, self.name
                )
                self._dependency_errors.append(msg)
                yatest_logger.warning(msg)

    def add_build_dep(self, project_path, platform, uid, tags):
        self._build_deps[uid] = {
            "project_path": project_path,
            "platform": platform,
            "tags": tags or [],
        }

    def get_build_deps(self):
        return self._build_deps.items()

    def get_build_dep_uids(self):
        return self._build_deps.keys()

    def change_build_dep_uids(self, uids_map):
        for uid in self.get_build_dep_uids():
            if uid in uids_map:
                self._build_deps[uids_map[uid]] = self._build_deps.pop(uid)

    def set_split_params(self, modulo, modulo_index, test_file):
        self._modulo = modulo
        self._modulo_index = modulo_index
        self._split_file_name = test_file

    def get_split_params(self):
        return self._modulo, self._modulo_index, self._split_file_name

    def get_fork_mode(self):
        return self.meta.fork_mode

    def fork_test_files_requested(self, opts):
        fork_test_files = (self.meta.fork_test_files or 'off') == "on"
        # TODO remove when DEVTOOLS-6560 is dones
        # test_files_filter option implies FORK_TEST_FILES()
        return fork_test_files or (self.supports_fork_test_files and opts.test_files_filter)

    def get_fork_partition_mode(self):
        return self.meta.test_partition or 'SEQUENTIAL'

    def get_test_files(self, opts=None):
        file_filters = set(getattr(opts, 'test_files_filter', []))
        # This is an ad hoc optimization for filtering suites with huge
        # amount of tests in different files with specified FORK_TEST_FILES() macro.
        # Tests may use recipes and every chunk will set up environment
        # before listing, which makes task to run one certain test extremely slow.
        # This suite contains at least one file for sure, because it was discovered
        # and passed suite filtering.
        if file_filters:
            return [f for f in self.meta.test_files if f in file_filters]
        else:
            return self.meta.test_files

    def get_computed_test_names(self, opts):
        return []

    @property
    def tags(self):
        return sorted(set([_f for _f in self.meta.tag if _f]))

    @property
    def default_requirements(self):
        return {
            'network': 'restricted',
        }

    @property
    def requirements(self):
        return self._original_requirements

    @property
    def _original_requirements(self):
        # only to generate uid and get requirements for autocheck report
        if self._requirements_map is not None:
            return self._requirements_map

        req = {}
        for entry in [_f for _f in self.meta.requirements if _f]:
            if ":" not in entry:
                yatest_logger.warning("Bad requirement syntax: %s in %s", entry, self.project_path)
                continue

            name_original, val_original = entry.strip().split(":", 1)
            name_lower, val_lower = name_original.lower(), val_original.lower()

            if name_lower not in devtools.ya.test.const.TestRequirements.enumerate():
                yatest_logger.warning("Unknown requirement: %s in %s", name_lower, self.project_path)
                continue
            elif name_lower in (
                devtools.ya.test.const.TestRequirements.SbVault,
                devtools.ya.test.const.TestRequirements.YavSecret,
                devtools.ya.test.const.TestRequirements.PortoLayers,
            ):
                req_value = val_original
            elif name_lower in (
                devtools.ya.test.const.TestRequirements.DiskUsage,
                devtools.ya.test.const.TestRequirements.RamDisk,
            ):
                req_value = mr.resolve_value(val_lower)
            # XXX
            elif name_lower == devtools.ya.test.const.TestRequirements.Ram:
                if val_lower == devtools.ya.test.const.TestRequirementsConstants.All:
                    req_value = val_lower
                else:
                    req_value = mr.resolve_value(val_lower)
            elif name_lower == devtools.ya.test.const.TestRequirements.Network:
                req_value = val_lower
            elif name_lower == devtools.ya.test.const.TestRequirements.Dns:
                req_value = val_lower
            elif name_lower == devtools.ya.test.const.TestRequirements.Kvm:
                req_value = True
            else:
                if val_lower == devtools.ya.test.const.TestRequirementsConstants.All:
                    req_value = val_lower
                else:
                    try:
                        req_value = int(val_lower)
                        if req_value < 1:
                            req_value = None
                    except ValueError:
                        req_value = None

            if req_value is not None:
                req[name_lower] = req_value
            else:
                yatest_logger.warning("Cannot convert '%s' to the proper requirement value", val_lower)

        self._requirements_map = req
        return self._requirements_map

    @property
    def requires_ram_disk(self):
        return "ram_disk" in self.requirements

    @property
    def supports_test_parameters(self):
        return False

    @property
    def supports_clean_environment(self):
        return True

    @property
    def supports_canonization(self):
        return True

    @property
    def supports_fork_test_files(self):
        return False

    @property
    def cache_test_results(self):
        """
        Must be used carefully.
        If suite support steady uid it will be always cached, until --retest is specified.
        """
        return False

    @property
    def supports_allure(self):
        return False

    @property
    def supports_coverage(self):
        """
        Inject coverage resolve nodes if a suite supports coverage
        """
        return False

    @property
    def is_test_built_in(self):
        # Returns true if the test is embedded into the wrapper.
        return False

    def get_configuration_errors(self):
        errors = []
        default_timeout = devtools.ya.test.const.TestSize.get_default_timeout(self.test_size)
        if self.timeout > default_timeout:
            errors.append(
                "Test timeout {} exceeds maximum allowed timeout {} for size {}".format(
                    self.timeout, default_timeout, self.test_size
                )
            )
        return errors

    @property
    def salt(self):
        return None

    def get_split_factor(self, opts):
        if opts and opts.testing_split_factor:
            return opts.testing_split_factor
        default_split_factor = 10 if self.get_fork_mode() in ["tests", "subtests"] else 1
        return int(self.meta.split_factor) if self.meta.split_factor else default_split_factor

    def get_skipped_reason(self):
        reason = self.meta.skip_test
        if reason:
            return "skipped for reason '{}'".format(reason)

    def get_resources(self, opts):
        return set()

    def get_global_resources(self):
        return sorted('::'.join(i) for i in self.meta.global_resources.items())

    def get_ios_runtime(self):
        return self.meta.test_ios_runtime_type

    def get_ios_device_type(self):
        return self.meta.test_ios_device_type

    def get_android_apk_activity(self):
        return self.meta.android_apk_test_activity

    def get_python_bin(self, opts=None):
        return external_tools.ToolsResolver.get_python_bin(opts, self.global_resources)

    def get_python_library(self, opts=None):
        return external_tools.ToolsResolver.get_python_lib(opts, self.global_resources)


class PythonTestSuite(AbstractTestSuite):
    """
    Support for tests that are run via separate python wrappers
    """

    @property
    def supports_test_parameters(self):
        return True

    def binary_path(self, root):
        """
        File with tests
        :return:
        """
        return os.path.join(self._source_folder_path(root), self.meta.tested_project_name)

    def _tested_file_rel_path(self):
        return self.binary_path('')

    @property
    def is_test_built_in(self):
        # Test is built-in into the wrapper.
        # It's a hint to test machinery to run wrapper directly with gdb if it's requested.
        return True


class StyleTestSuite(PythonTestSuite):
    def get_test_related_paths(self, arc_root, opts):
        return []

    def support_retries(self):
        return False


class PerformedTestSuite(AbstractTestSuite):
    def __init__(
        self,
        name=None,
        project_path=None,
        size=devtools.ya.test.const.TestSize.Small,
        tags=None,
        target_platform_descriptor=None,
        suite_type=None,
        suite_ci_type=None,
        multi_target_platform_run=False,
        # kept for backward compatibility with devtools/common/reports which is used in SB tasks
        # TODO delete once devtools/common/reports is deployed
        uid=None,
    ):  # noqa
        dart = {'TAG': tags or []}
        super(PerformedTestSuite, self).__init__(
            dart,
            target_platform_descriptor=target_platform_descriptor,
            multi_target_platform_run=multi_target_platform_run,
        )
        self._name = name
        self._suite_type = suite_type
        self._suite_ci_type = suite_ci_type
        self._project_path = project_path
        if self._project_path:
            self._project_path = self._project_path.strip("/")
        self._target_platform_descriptor = target_platform_descriptor
        self._multi_target_platform_run = multi_target_platform_run
        self._test_size = size

    @property
    def name(self):
        return self._name

    def get_type(self):
        return self._suite_type

    def get_ci_type_name(self):
        return self._suite_ci_type

    @property
    def project_path(self):
        return self._project_path

    @property
    def test_size(self):
        return self._test_size


class DiffTestSuite(AbstractTestSuite):
    def __init__(self, project_path, revision, target_platform_descriptor):
        super(DiffTestSuite, self).__init__({}, target_platform_descriptor=target_platform_descriptor)
        self._project_path = project_path
        self._revision = revision
        self.add_python_before_cmd = False

    def get_type(self):
        return DIFF_TEST_TYPE

    @property
    def name(self):
        return DIFF_TEST_TYPE

    @property
    def project_path(self):
        return self._project_path

    def get_test_dependencies(self):
        return []

    def get_test_related_paths(self, arc_root, opts):
        paths = super(DiffTestSuite, self).get_test_related_paths(arc_root, opts)
        if opts and opts.remove_implicit_data_path:
            return paths
        paths.insert(0, os.path.join(arc_root, "devtools", "ya"))
        return paths

    @property
    def default_requirements(self):
        # run_diff_test takes revision and switches canondata/results.json
        # in test environment. However, test machinery doesn't know about this trick
        # and can't properly prepare environment (there will be download nodes for non-actual
        # resources from canondata/results.json, which will be replaced in node's runtime)
        # That's why we allow this node to download canon data resources. But it's a hack and need to be fixed.
        return {
            'network': 'full',
        }

    @property
    def supports_canonization(self):
        return False

    def support_retries(self):
        return False

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir('$(BUILD_ROOT)', self.project_path, self.name)
        output_dir = os.path.join(test_work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME)
        cmd = devtools.ya.test.util.tools.get_test_tool_cmd(
            opts, 'run_diff_test', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            "--suite-name",
            type(self).__name__,
            "--project-path",
            self.project_path,
            "--output-dir",
            output_dir,
            "--work-dir",
            test_work_dir,
            "--trace-path",
            os.path.join(test_work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            "--source-root",
            opts.arc_root,
            "--revision",
            self._revision,
        ]
        for flt in opts.tests_filters + opts.test_files_filter:
            cmd += ["--filter", flt]
        if opts.custom_fetcher:
            cmd += ["--custom-fetcher", opts.custom_fetcher]
        cmd += devtools.ya.test.util.shared.get_oauth_token_options(opts)

        return cmd

    @property
    def supports_clean_environment(self):
        return False

    def get_list_cmd(self, arc_root, build_root, opts):
        return self.get_run_cmd(opts) + ["--list"]

    @classmethod
    def list(cls, cmd, cwd):
        result = []
        list_cmd_result = process.execute(cmd, cwd=cwd)
        try:
            tests = json.loads(list_cmd_result.std_err)
            for t in tests:
                result.append(
                    test_common.SubtestInfo(
                        test_common.strings_to_utf8(t["class"]), test_common.strings_to_utf8(t["test"])
                    )
                )
            return result
        except Exception as e:
            ei = sys.exc_info()
            six.reraise(ei[0], "{}\nListing stderr: {}".format(str(e), list_cmd_result.std_err), ei[2])

    def _source_folder_path(self, root):
        raise NotImplementedError()


class SkippedTestSuite(object):
    def __init__(self, original_suite):
        assert original_suite.is_skipped()
        object.__setattr__(self, "_original_suite", original_suite)

    def __getattribute__(self, name):
        if name in [
            "fork_test_files_requested",
            "get_run_cmd",
            "is_skipped",
            "requirements",
            "support_splitting",
            "supports_canonization",
            "supports_fork_test_files",
        ]:
            return object.__getattribute__(self, name)
        return getattr(object.__getattribute__(self, "_original_suite"), name)

    def __setattr__(self, name, value):
        setattr(object.__getattribute__(self, "_original_suite"), name, value)

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        test_work_dir = test_common.get_test_suite_work_dir('$(BUILD_ROOT)', self.project_path, self.name)
        cmd = devtools.ya.test.util.tools.get_test_tool_cmd(
            opts, 'run_skipped_test', self.global_resources, wrapper=True, run_on_target_platform=True
        ) + [
            "--trace-path",
            os.path.join(test_work_dir, devtools.ya.test.const.TRACE_FILE_NAME),
            "--reason",
            self.get_comment(),
        ]
        return cmd

    def fork_test_files_requested(self, opts):
        return (self.meta.fork_test_files or 'off') == "on"

    def supports_canonization(self):
        return False

    def support_splitting(self, opts=None):
        return False

    def supports_fork_test_files(self):
        return False

    def is_skipped(self):
        return True

    @property
    def requirements(self):
        # distbuild fails if finds some requirements, e.g. ram_disk:8, even for skipped tests, TODO: DEVTOOLS-5005 - stop sending skipped tests to distbs
        return {}
