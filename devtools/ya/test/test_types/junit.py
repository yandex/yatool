import sys
import logging
import subprocess as sp
import os
import exts.yjson as json
import base64

import six

import devtools.ya.test.test_types.common as test_types
from library.python import func

from devtools.ya.jbuild.gen import base
from devtools.ya.test.common import ytest_common_tools as yct
from devtools.ya.test.common import ytest_common_tools as yc
import devtools.ya.test.common as test_common
import devtools.ya.test.const as test_const
import devtools.ya.test.util.tools as test_tools
import devtools.ya.jbuild.gen.makelist_parser2 as mp2

import yalibrary.graph.base as graph_base
import yalibrary.graph.commands as graph_commands
import yalibrary.graph.const as graph_consts

logger = logging.getLogger(__name__)


JAVA_TEST_TYPE = "java"


def strip_root(s):
    return s[3:]


class JavaTestSuite(test_types.AbstractTestSuite):
    test_list_output_path = 'junit_tests_list.txt'

    def __init__(self, meta, target_platform_descriptor=None, multi_target_platform_run=False):
        super(JavaTestSuite, self).__init__(
            meta,
            target_platform_descriptor=target_platform_descriptor,
            multi_target_platform_run=multi_target_platform_run,
        )
        self.jvm_args = []
        self.init_jvm_args()

        self.props = []
        self.init_props()

        self.initialized = False
        self.classpath_cmd_type = self.meta.java_classpath_cmd_type

        self.default_vars = mp2.default_vars(graph_base.hacked_normpath(self.project_path))
        self.python = None
        self.jdk_resource = None
        self.jdk_for_tests_resource = None
        self.jacoco_agent_resource = None
        self.jdk_resource_prefix = self.meta.jdk_resource_prefix or 'JDK_NOT_FOUND'
        self.jdk_for_tests_resource_prefix = self.meta.jdk_for_tests_resource_prefix or 'JDK_FOR_TESTS_NOT_FOUND'

        test_classpath = [strip_root(p) for p in self.meta.test_classpath.split()]
        self.classpath = [os.path.join(graph_consts.BUILD_ROOT, p) for p in test_classpath]
        self.deps = test_classpath[:]
        self.classpath_package_files = self.get_classpath_package_files(test_classpath)

        test_libpath = [strip_root(p) for p in self.meta.test_classpath_deps.split()]
        self.libpath = [os.path.join(graph_consts.BUILD_ROOT, os.path.dirname(p)) for p in test_libpath]
        self.deps += test_libpath

        self.tests_jar = os.path.join(graph_consts.BUILD_ROOT, self.meta.test_jar)
        self.classpath_file = os.path.splitext(self.tests_jar)[0] + '.test.cpf'

        self.experimental_fork = self.meta.experimental_fork

    def init_from_opts(self, opts):
        self.jdk_resource = base.resolve_jdk(
            self.global_resources,
            prefix=self.jdk_resource_prefix,
            prefix_for_tests=self.jdk_for_tests_resource_prefix,
            opts=opts,
        )
        self.jdk_for_tests_resource = base.resolve_jdk(
            self.global_resources,
            prefix=self.jdk_resource_prefix,
            prefix_for_tests=self.jdk_for_tests_resource_prefix,
            opts=opts,
            for_test=True,
        )
        self.jacoco_agent_resource = base.resolve_jacoco_agent(self.global_resources, opts)
        self.jvm_args.append('-DJAVA=' + test_tools.jdk_tool('java', jdk_path=self.jdk_for_tests_resource))
        self.python = test_common.get_python_cmd(opts=opts)
        self.initialized = True

    def get_classpath(self):
        return self.classpath

    def get_direct_deps(self, deps):
        return [os.path.relpath(jar_file, graph_consts.BUILD_ROOT) for jar_file in deps]

    def get_classpath_package_files(self, deps):
        return [os.path.splitext(jar_file)[0] + ".cpsf" for jar_file in deps]

    def init_props(self):
        if self.meta.t_system_properties:
            self.props = json.loads(base64.b64decode(self.meta.t_system_properties))

    def init_jvm_args(self):
        # Default JVM args
        self.jvm_args.extend(['-Dfile.encoding=utf8', '-Dya.make'])
        for k, v in sorted(mp2.default_vars(self.project_path).items()):
            self.jvm_args.append('-D{}={}'.format(k, v))

        # args from JVM_ARGS
        self.jvm_args.extend(self.meta.t_jvm_args)

    def get_cmd_jvm_args(self, opts):
        jvm_args = self.jvm_args[:]

        # args from cmdline
        if getattr(opts, 'jvm_args', []):
            jvm_args.extend(mp2.split(opts.jvm_args, replace_escaped_whitespaces=False))

        return [mp2.replace_vars(x, self.default_vars) for x in jvm_args]

    def get_cmd_props(self, opts):
        props = self.props[:]

        for x in opts.properties_files:
            if os.path.exists(x) and os.path.isfile(x):
                props.append({'type': 'file', 'path': os.path.abspath(x)})

            else:
                logger.error('Properties file %s does not exist', x)

        for k, v in opts.properties.items():
            props.append({'type': 'inline', 'key': k, 'value': v})

        return six.ensure_str(base64.b64encode(six.ensure_binary(json.dumps(props, encoding='utf-8', sort_keys=True))))

    def support_splitting(self, opts=None):
        return True

    def support_list_node(self):
        """
        Does test suite support list_node before test run
        """
        return True

    def support_retries(self):
        return True

    @property
    def supports_allure(self):
        return True

    @property
    def supports_coverage(self):
        return True

    @property
    def supports_test_parameters(self):
        return True

    def get_type(self):
        return JAVA_TEST_TYPE

    @property
    def class_type(self):
        return test_const.SuiteClassType.REGULAR

    def get_test_dependencies(self):
        assert self.initialized
        return list(set(_f for _f in self.deps + self._custom_dependencies if _f))

    def get_run_cmd(self, opts, retry=None, for_dist_build=True):
        return self._get_run_cmd(graph_consts.SOURCE_ROOT, graph_consts.BUILD_ROOT, opts, retry=retry)

    def get_run_cmd_inputs(self, opts):
        return super(JavaTestSuite, self).get_run_cmd_inputs(opts)

    def get_list_cmd(self, arc_root, build_root, opts):
        assert self.initialized

        return self._make_run_test_cmd(
            arc_root,
            self.jdk_resource,
            build_root=build_root,
            output=self.test_list_output_path,
            filters=opts.tests_filters + self._additional_filters,
            fork_subtests=self.get_fork_mode() == 'subtests',
            list_tests=True,
            props=self.get_cmd_props(opts),
            jvm_args=self.get_cmd_jvm_args(opts),
            coverage=opts.java_coverage,
            junit_args=opts.junit_args,
        ).cmd

    def get_list_cmd_inputs(self, opts):
        return super(JavaTestSuite, self).get_run_cmd_inputs(opts)

    def get_list_cwd(self, arc_root, build_root, opts):
        return build_root

    @classmethod
    def list(cls, cmd, cwd):
        sp.check_output(cmd, cwd=cwd)
        with open(os.path.join(cwd, cls.test_list_output_path)) as out:
            return cls._get_subtests_info(out.read())

    @classmethod
    def _get_subtests_info(cls, out):
        try:
            return [yct.SubtestInfo.from_json(json.loads(test_line)) for test_line in out.splitlines()]
        except ValueError:
            # old format
            return list(map(yct.SubtestInfo.from_str, out.strip().split('\n') if out.strip() else []))
        except Exception as e:
            ei = sys.exc_info()
            six.reraise(ei[0], "{}\nListing output: {}".format(str(e), out), ei[2])

    def _get_run_cmd(self, arc_root, build_root, test_params, retry=None):
        assert self.initialized

        suite_work_dir = yc.get_test_suite_work_dir(
            graph_consts.BUILD_ROOT,
            self.project_path,
            self.name,
            retry=retry,
            split_count=self._modulo,
            split_index=self._modulo_index,
            target_platform_descriptor=self.target_platform_descriptor,
            multi_target_platform_run=self.multi_target_platform_run,
            remove_tos=test_params.remove_tos,
        )
        test_outputs_root = os.path.join(suite_work_dir, test_const.TESTING_OUT_DIR_NAME)
        trace_file_path = os.path.join(suite_work_dir, test_const.TRACE_FILE_NAME)
        runner_log_file_path = os.path.join(test_outputs_root, 'run.log')
        if test_const.YaTestTags.JavaTmpInRamDisk in self.tags:
            # it's resolved in run_junit.py from context file
            tests_tmp_dir = "${YA_TEST_JAVA_TMP_DIR}"
        else:
            tests_tmp_dir = os.path.join(suite_work_dir, 'tests_tmp_dir')

        return self._make_run_test_cmd(
            arc_root,
            self.jdk_for_tests_resource,
            build_root=build_root,
            sandbox_resources_root=suite_work_dir,
            test_outputs_root=test_outputs_root,
            output=trace_file_path,
            filters=test_params.tests_filters + self._additional_filters,
            fork_subtests=self.get_fork_mode() == 'subtests',
            runner_log_path=runner_log_file_path,
            tests_tmp_dir=tests_tmp_dir,
            allure=getattr(test_params, 'allure_report', False),
            props=self.get_cmd_props(test_params),
            jvm_args=self.get_cmd_jvm_args(test_params),
            coverage=test_params.java_coverage,
            junit_args=test_params.junit_args,
            suite_work_dir=suite_work_dir,
            params=getattr(test_params, "test_params"),
            ytrace_file=trace_file_path,
        ).cmd

    def binary_path(self, root):
        return None

    def has_prepare_test_cmds(self):
        return True

    @func.memoize()
    def get_prepare_test_cmds(self):
        inputs = [
            graph_base.hacked_path_join(graph_consts.SOURCE_ROOT, 'build', 'scripts', 'mkdir.py'),
            graph_base.hacked_path_join(graph_consts.SOURCE_ROOT, 'build', 'scripts', 'run_junit.py'),
            graph_base.hacked_path_join(graph_consts.SOURCE_ROOT, 'build', 'scripts', 'writer.py'),
            graph_base.hacked_path_join(graph_consts.SOURCE_ROOT, 'build', 'scripts', 'process_command_files.py'),
        ]
        cmds = [
            {
                'cmd_args': self.python
                + [
                    graph_base.hacked_path_join(graph_consts.SOURCE_ROOT, 'build', 'scripts', 'mkdir.py'),
                    os.path.dirname(self.classpath_file),
                ]
            },
            {
                'cmd_args': self.python
                + [
                    graph_base.hacked_path_join(graph_consts.SOURCE_ROOT, 'build', 'scripts', 'writer.py'),
                    '--file',
                    self.classpath_file,
                    '-m',
                    '--ya-start-command-file',
                ]
                + list(map(lambda p: os.path.relpath(p, graph_consts.BUILD_ROOT), self.classpath))
                + [
                    '--ya-end-command-file',
                ]
            },
        ]
        return cmds, inputs

    @property
    def smooth_shutdown_signals(self):
        return ["SIGUSR1"]

    def _make_run_test_cmd(
        self,
        source_root,
        jdk_resource,
        build_root=None,
        sandbox_resources_root=None,
        test_outputs_root=None,
        output=None,
        filters=None,
        fork_subtests=False,
        list_tests=False,
        runner_log_path=None,
        tests_tmp_dir=None,
        allure=False,
        props=None,
        jvm_args=None,
        cwd=None,
        coverage=False,
        suite_work_dir=None,
        params=None,
        junit_args=None,
        ytrace_file=None,
    ):
        properties = []

        if allure:
            allure_dir = os.path.join(suite_work_dir or os.curdir, "allure")
            properties.append('-Dallure.results.directory=' + allure_dir)

        if tests_tmp_dir:
            properties.append('-Djava.io.tmpdir=' + tests_tmp_dir)

        if self.libpath:
            properties.append('-Djava.library.path=' + ':'.join(self.libpath))
            properties.append('-Djna.library.path=' + ':'.join(self.libpath))

        if not jvm_args:
            jvm_args = []

        if not coverage or not suite_work_dir:
            coverage = []
        else:
            coverage = [
                '-javaagent:{}=output=file,destfile={}'.format(
                    test_tools.jacoco_agent_tool(self.jacoco_agent_resource),
                    os.path.join(suite_work_dir, 'java.coverage', 'report.exec'),
                )
            ]

        cmd = (
            self.python
            + [graph_consts.SOURCE_ROOT + '/build/scripts/run_junit.py']
            + self.python
            + [graph_consts.SOURCE_ROOT + '/build/scripts/unpacking_jtest_runner.py']
        )
        if self.classpath_cmd_type:
            cmd += ['--classpath-option-type', self.classpath_cmd_type.lower()]

        if ytrace_file:
            cmd += ['--trace-file', ytrace_file]

        cmd += (
            [
                '--jar-binary',
                test_tools.jdk_tool('jar', jdk_path=jdk_resource),
                '--tests-jar-path',
                self.tests_jar,
                test_tools.jdk_tool('java', jdk_path=jdk_resource),
            ]
            + coverage
            + properties
            + jvm_args
            + [
                '-classpath',
                self.classpath_file if self.classpath_file.endswith('.jar') else '@' + self.classpath_file,
                'ru.yandex.devtools.test.Runner',
                '--tests-jar',
                self.tests_jar,
                '--source-root',
                source_root,
                '--modulo',
                str(self._modulo),
                '--modulo-index',
                str(self._modulo_index),
            ]
        )
        if self.experimental_fork == 'yes':
            cmd.append('--experimental-fork')

        if build_root:
            cmd.extend(['--build-root', build_root])

        if sandbox_resources_root:
            cmd.extend(['--sandbox-resources-root', sandbox_resources_root])

        if test_outputs_root:
            cmd.extend(['--test-outputs-root', test_outputs_root])

        if output:
            cmd.extend(['--output', output])

        if filters:
            for f in filters:
                cmd.extend(['--filter', f])

        if fork_subtests:
            cmd.append('--fork-subtests')

        if list_tests:
            cmd.append('--list')

        if runner_log_path:
            cmd.extend(['--runner-log-path', runner_log_path])

        if allure:
            cmd.append('--allure')

        if props:
            cmd.extend(['--properties', props])

        if params:
            for key, value in six.iteritems(params):
                cmd += ["--test-param", "{}={}".format(key, value)]

        if junit_args:
            cmd += junit_args

        return graph_commands.Cmd(cmd, cwd, [])


class Junit5TestSuite(JavaTestSuite):
    pass
