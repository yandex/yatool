# coding: utf-8
import collections
import copy
import getpass
import json
import logging
import os
import socket

import time

import app_config
import devtools.ya.build.gen_plan as gen_plan
import devtools.ya.core.config
import devtools.ya.core.error
import devtools.ya.core.imprint.imprint as imprint
import devtools.ya.core.profiler
import devtools.ya.core.yarg

import devtools.ya.test.dependency.mds_storage as mds_storage
import devtools.ya.test.dependency.sandbox_resource as sandbox_resource
import devtools.ya.test.dependency.testdeps as testdeps
import devtools.ya.test.dependency.uid as uid_gen
import devtools.ya.test.error as test_error
from . import coverage
from . import cmdline
from . import fuzzing

from . import sandbox as sandbox_node

import exts.fs
import exts.func
import exts.os2
import exts.strings
import exts.timer
import exts.uniq_id
import exts.windows

import devtools.ya.test.canon.data as canon_data
import devtools.ya.test.common as test_common
import devtools.ya.test.common.ytest_common_tools as ytest_common_tools
import devtools.ya.test.const
import devtools.ya.test.filter as test_filter
import devtools.ya.test.system.env as sysenv
import devtools.ya.test.test_types.fuzz_test as fuzz_test
import devtools.ya.test.util.shared as util_shared
import devtools.ya.test.util.tools as util_tools

import yalibrary.last_failed.last_failed as last_failed
import yalibrary.upload.consts
import yalibrary.fetcher.uri_parser as uri_parser

import typing as tp

if tp.TYPE_CHECKING:
    from test.facility.containers import Suite  # noqa

logger = logging.getLogger(__name__)

# Don't use sys.maxint - it can't be serialized to yson (ya:yt mode)
MAX_TIMEOUT = 2**32

FETCH_DOCKER_IMAGE_SCRIPT = '$(SOURCE_ROOT)/build/scripts/fetch_from_docker_repo.py'
FETCH_FROM_MDS_SCRIPT = '$(SOURCE_ROOT)/build/scripts/fetch_from_mds.py'
FETCH_FROM_SCRIPT = '$(SOURCE_ROOT)/build/scripts/fetch_from.py'
SCRIPT_APPEND_FILE = '$(SOURCE_ROOT)/build/scripts/append_file.py'
PROJECTS_FILE_INPUTS = [SCRIPT_APPEND_FILE]


class _DependencyException(Exception):
    pass


@exts.func.lazy
def get_mds_storage(storage_root):
    return mds_storage.MdsStorage(storage_root)


def add_list_node(opts, suite):
    return opts.list_before_test and suite.support_list_node()


def inject_download_docker_image_node(graph, image, opts):
    link, tag = image

    fake_id = 5
    uid = "docker-image-{}-{}".format(imprint.combine_imprints(link, tag), fake_id)
    output_dir = os.path.join("images", imprint.combine_imprints(tag))
    output_image_path = os.path.join("$(BUILD_ROOT)", output_dir, 'image.tar')
    output_info_path = os.path.join("$(BUILD_ROOT)", output_dir, 'image_info.json')
    resource_id = uri_parser.get_docker_resource_id(link)
    preloaded_path = "$(RESOURCE_ROOT)/docker/{}/resource".format(resource_id)

    cmd = test_common.get_python_cmd(opts=opts) + [
        FETCH_DOCKER_IMAGE_SCRIPT,
        '--link',
        link,
        '--tag',
        tag,
        '--output-image-path',
        output_image_path,
        '--output-info-path',
        output_info_path,
        '--preloaded-path',
        preloaded_path,
    ]

    node_requirements = gen_plan.get_requirements(opts)

    if opts.use_distbuild:
        timeout = 900
    else:
        timeout = 0

    if not graph.get_node_by_uid(uid):
        node = {
            "timeout": timeout,
            "node-type": devtools.ya.test.const.NodeType.DOWNLOAD,
            "broadcast": False,
            "inputs": [],
            "uid": uid,
            "cwd": "$(BUILD_ROOT)",
            "priority": 0,
            "deps": [],
            "env": sysenv.get_common_env().dump(),
            "target_properties": {},
            "outputs": [output_image_path, output_info_path],
            'kv': {
                "p": "IM",
                "pc": 'light-cyan',
                "show_out": True,
            },
            "requirements": node_requirements,
            "resources": [
                {"uri": link},
            ],
            "cmds": [
                {
                    "cmd_args": cmd,
                    "cwd": "$(BUILD_ROOT)",
                },
            ],
        }

        graph.append_node(node, add_to_result=False)

    return uid


def inject_mds_resource_to_graph(graph, resource, opts):
    fake_id = 0
    uid_data = [resource, str(fake_id)]
    if opts.canonization_backend:
        backend = opts.canonization_backend
        uid_data.append(backend)
    else:
        backend = devtools.ya.test.const.DEFAULT_CANONIZATION_BACKEND
    uid = uid_gen.get_uid(uid_data, 'mds-resource')

    if not graph.get_node_by_uid(uid):
        storage = get_mds_storage("$(BUILD_ROOT)")
        log_path = storage.get_resource_download_log_path(resource)
        output_file = storage.get_storage_resource_path(resource)

        fetch_cmd = test_common.get_python_cmd(opts=opts) + [
            FETCH_FROM_MDS_SCRIPT,
            "--key",
            resource,
            "--entrypoint",
            backend,
            "--rename-to",
            output_file,
            "--log-path",
            log_path,
            "--scheme",
            opts.canonization_scheme,
        ]

        node = {
            "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
            "broadcast": False,
            "inputs": [FETCH_FROM_MDS_SCRIPT, FETCH_FROM_SCRIPT],
            "uid": uid,
            "cwd": "$(BUILD_ROOT)",
            "priority": 0,
            "deps": [],
            "env": sysenv.get_common_env().dump(),
            "target_properties": {},
            "outputs": [
                output_file,
                log_path,
            ],
            'kv': {
                "p": "DL",
                "pc": 'cyan',
                "show_out": True,
            },
            "requirements": gen_plan.get_requirements(opts, {"network": "full"}),
            "cmds": [{"cmd_args": fetch_cmd, "cwd": "$(BUILD_ROOT)"}],
        }
        graph.append_node(node, add_to_result=False)

    return uid


# Throws _DependencyException
def get_test_build_deps_or_throw(suite):
    errors = suite.get_dependency_errors()
    if errors:
        raise _DependencyException(errors[0])
    return suite.get_build_dep_uids()


def _get_env_arg(opts, suite):
    test_env = []
    if suite.env:
        test_env.extend(suite.env)

    if opts and getattr(opts, 'test_env'):
        test_env.extend(opts.test_env)

    gkv = dict(it.split('::', 1) for it in suite.get_global_resources())

    def replace_global_resource(x):
        for k in gkv.keys():
            x = x.replace('$' + k, gkv[k])
        return x

    arg = []
    for s in map(replace_global_resource, test_env):
        if "=" in s:
            arg.extend(("--env", s))
        elif s in os.environ:
            arg.extend(("--env", s + '=' + os.environ[s]))
    return arg


# XXX WIP
class TestFramer(object):
    def __init__(self, arc_root, graph, platform, add_conf_error, opts):
        self.arc_root = arc_root
        self.graph = graph
        self.opts = opts
        self.platform = platform
        self.distbuild_runner = self.opts.use_distbuild
        self.add_conf_error = add_conf_error
        self.context_generator_cache = {}

    def prepare_suites(self, suites):
        return configure_suites(suites, self._prepare_suite, 'suites-configuration', add_error_func=self.add_conf_error)

    def _prepare_suite(self, suite):
        # type: (Suite) -> None

        # XXX Bad design, see YA-1440
        suite.init_from_opts(self.opts)

        suite.setup_dependencies(self.graph)

        uid = self.inject_test_context_generator_node(suite.target_platform_descriptor)
        suite.add_build_dep(project_path=None, platform=suite.target_platform_descriptor, uid=uid, tags=[])

        # it's fake output dir. It's constructed without using retries/split_index/split_file
        out_dir = test_common.get_test_suite_work_dir(
            '$(BUILD_ROOT)',
            suite.project_path,
            suite.name,
            target_platform_descriptor=suite.target_platform_descriptor,
            multi_target_platform_run=suite.multi_target_platform_run,
            remove_tos=self.opts.remove_tos,
        )

        if coverage.cpp.is_cpp_coverage_requested(self.opts):
            suite.set_timeout(int(suite.timeout * devtools.ya.test.const.COVERAGE_TESTS_TIMEOUT_FACTOR))

        if self.opts.test_size_timeouts:
            suite.set_timeout(self.opts.test_size_timeouts.get(suite.test_size, suite.timeout))

        if self.opts.fuzz_node_timeout is not None:
            logger.info("Timeout for fuzzing test %s is %s", suite, self.opts.fuzz_node_timeout)
            suite.set_timeout(self.opts.fuzz_node_timeout)

        if self.opts.test_disable_timeout:
            if self.opts.cache_tests or self.distbuild_runner:
                raise devtools.ya.core.yarg.FlagNotSupportedException(
                    "Cannot turn timeout off when using test cache or running on distbuild"
                )
            logger.info("Timeout for test %s is turned off", suite)
            # Timeout is not expected to be None - use MAX_TIMEOUT instead of disabling timeout
            suite.set_timeout(MAX_TIMEOUT)

        suite.uid = get_suite_uid(
            suite,
            self.graph,
            self.arc_root,
            self.opts,
            self.distbuild_runner,
            out_dir,
        )
        if suite.special_runner == 'yt' and self.opts.run_tagged_tests_on_yt and not suite.is_skipped():
            suite.uid = "yt-{}".format(suite.uid)

        # Skipped and non-skipped suite must provide same uid,
        # that's why all uid-affecting configuration should be done before
        # setting suite.uid.
        # Uid-unaffecting processing should be done only for non-skipped suites.
        if not suite.is_skipped():
            # XXX setup chunks for be able to inject split tests
            suite.chunks = suite.gen_suite_chunks(self.opts)

            # Setup canonical resources ones
            if suite.supports_canonization:
                deps = set()
                # MDS canonical resources
                for resource in testdeps.get_test_mds_resources(suite):
                    deps.add(inject_mds_resource_to_graph(self.graph, resource, self.opts))
                suite.mds_resource_deps = sorted(deps)

    def inject_test_context_generator_node(self, platform_descriptor):
        if platform_descriptor not in self.context_generator_cache:
            flags = dict(self.platform.get('flags', {}))
            flags.update(self.opts.flags)
            # Restrict build flags usage in tests. Each flag affects test uid.
            flags = {k: v for k, v in flags.items() if k in devtools.ya.test.const.BUILD_FLAGS_ALLOWED_IN_CONTEXT}

            # Common context contains suite-agnostic data, mostly test build info
            context = {
                'build_type': self.platform.get('build_type', self.opts.build_type),
                'flags': flags,
            }
            sanitizer = flags.get('SANITIZER_TYPE', self.opts.sanitize)
            if sanitizer:
                context['sanitizer'] = sanitizer

            output = '$(BUILD_ROOT)/' + devtools.ya.test.const.COMMON_CONTEXT_FILE_NAME
            ctx_str = json.dumps(context, indent=2, sort_keys=True, separators=(",", ": "))
            uid = uid_gen.get_uid([output, ctx_str], 'test-ctx-gen')

            tags, platform = gen_plan.prepare_tags(self.platform, {}, self.opts)

            node = {
                "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
                'uid': uid,
                'broadcast': False,
                'cmds': [
                    {
                        'cmd_args': [
                            '$(PYTHON)/python',
                            SCRIPT_APPEND_FILE,
                            output,
                        ]
                        + ctx_str.split('\n'),
                    },
                ],
                'deps': [],
                'inputs': [SCRIPT_APPEND_FILE],
                'kv': {'p': 'CP', 'pc': 'light-blue'},
                'outputs': [output],
                'priority': 0,
                'requirements': gen_plan.get_requirements(self.opts, {'network': 'restricted'}),
                'cache': True,
                'type': 2,
                'tags': tags,
                'platform': platform,
            }

            self.graph.append_node(node, add_to_result=False)
            self.context_generator_cache[platform_descriptor] = uid

        return self.context_generator_cache[platform_descriptor]


def create_test_node(
    arc_root,
    suite,
    graph,
    platform_descriptor,
    custom_deps=None,
    opts=None,
    retry=None,
    split_test_factor=1,
    split_index=0,
    split_file=None,
):
    inline_diff = getattr(opts, 'inline_diff', False)
    backup = getattr(opts, 'backup_test_results', False)

    work_dir = test_common.get_test_suite_work_dir(
        '$(BUILD_ROOT)',
        suite.project_path,
        suite.name,
        retry,
        split_test_factor,
        split_index,
        target_platform_descriptor=suite.target_platform_descriptor,
        split_file=split_file,
        multi_target_platform_run=suite.multi_target_platform_run,
        remove_tos=opts.remove_tos,
    )
    outputs_map = util_shared.get_common_test_outputs(work_dir)

    outputs = list(outputs_map.values())

    test_output_dir = os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME)

    is_for_distbuild = opts.use_distbuild

    inputs = set(suite.get_run_cmd_inputs(opts))

    deps = list(get_test_build_deps_or_throw(suite))

    runner_cmd = util_tools.get_test_tool_cmd(
        opts, "run_test", suite.global_resources, run_on_target_platform=False
    ) + [
        "--meta",
        outputs_map["meta"],
        "--trace",
        outputs_map["trace"],
        "--timeout",
        str(suite.timeout),
        "--log-path",
        outputs_map["run_test_log"],
        "--test-size",
        suite.test_size,
        "--test-type",
        suite.get_type(),
        "--test-ci-type",
        suite.get_ci_type_name(),
        "--context-filename",
        "$(BUILD_ROOT)/" + devtools.ya.test.const.COMMON_CONTEXT_FILE_NAME,
        # "--log-level", "DEBUG"
    ]

    if opts and opts.test_prepare:
        runner_cmd += ["--prepare-only"]
        test_context_output = os.path.join(work_dir, devtools.ya.test.const.SUITE_CONTEXT_FILE_NAME)
        outputs.append(test_context_output)

    runner_cmd += cmdline.get_environment_relative_options(suite, opts)

    if opts and (opts.keep_temps or opts.show_test_cwd):
        runner_cmd += ["--show-test-cwd"]

    if opts and opts.keep_temps:
        runner_cmd += ["--dont-replace-roots", "--keep-temps"]

    if opts.remove_tos:
        runner_cmd += ["--remove-tos"]

    if opts.autocheck_mode:
        runner_cmd += ["--autocheck-mode"]

    if opts and opts.fuzzing:
        corpus_tar = os.path.join(work_dir, devtools.ya.test.const.GENERATED_CORPUS_DIR_NAME + ".tar")
        outputs.append(corpus_tar)
        runner_cmd += ["--fuzz-corpus-tar", corpus_tar]

    if opts and opts.store_original_tracefile:
        runner_cmd += ["--store-original-tracefile-tar", os.path.join(test_output_dir, 'debug.tracefile')]

    if opts and opts.test_debug:
        runner_cmd += ["--show-test-pid"]
        runner_cmd += ["--same-process-group"]
        runner_cmd += ["--test-stderr"]

    if exts.windows.on_win():
        gdb_require = False
    else:
        gdb_require = True
    if getattr(opts, 'debugger_requested'):
        runner_cmd += ["--same-process-group"]
        if opts.pdb:
            runner_cmd += ["--pdb"]
        if opts.gdb and suite.is_test_built_in:
            runner_cmd += ["--gdb-debug"]
            gdb_require = True
    if gdb_require:
        runner_cmd += ["--gdb-path", "$(GDB)/gdb/bin/gdb"]

    if opts and getattr(opts, "test_stderr"):
        runner_cmd += ["--test-stderr"]

    if opts and getattr(opts, "test_stdout"):
        runner_cmd += ["--test-stdout"]

    if opts and getattr(opts, "external_py_files"):
        runner_cmd += [
            "--create-root-guidance-file",
            f"--pycache-prefix={devtools.ya.core.config.pycache_path()}",
        ]

    if opts.custom_canondata_path:
        runner_cmd += ["--custom-canondata-path", opts.custom_canondata_path]

    # Don't add empty output if such type of python coverage can't be produced by node
    pydeps = False
    if opts.python_coverage:
        for u, output in coverage.rigel.get_suite_binary_deps(suite, graph):
            pydeps |= graph.is_target_python3(u)

        if opts.coverage_prefix_filter:
            runner_cmd += ["--coverage-prefix-filter", opts.coverage_prefix_filter]
        if opts.coverage_exclude_regexp:
            runner_cmd += ["--coverage-exclude-regexp", opts.coverage_exclude_regexp]

    if suite.supports_coverage:
        for cov_opt, required, extra_args, output_name in [
            ("java_coverage", True, ["--java-coverage-path"], "java.coverage.tar"),
            ("go_coverage", True, ["--go-coverage-path"], "go.coverage.tar"),
            ("python_coverage", pydeps, ["--python3-coverage-path"], "py3.coverage.tar"),
            ("ts_coverage", True, ["--ts-coverage-path"], "ts.coverage.tar"),
            ("nlg_coverage", True, ["--nlg-coverage-path"], "unified.coverage.tar"),
            ("sancov_coverage", True, ["--sancov-coverage", "--cpp-coverage-path"], "coverage.tar"),
            ("clang_coverage", True, ["--clang-coverage", "--cpp-coverage-path"], "coverage.tar"),
        ]:
            if getattr(opts, cov_opt, False) and required:
                filename = os.path.join(work_dir, output_name)
                runner_cmd += extra_args + [filename]
                outputs += [filename]

        if opts and getattr(opts, "fast_clang_coverage_merge"):
            log_path = os.path.join(work_dir, 'coverage_merge.log')
            runner_cmd += ["--fast-clang-coverage-merge", log_path]
            outputs += [log_path]

    if suite.supports_allure and getattr(opts, 'allure_report', None):
        allure_path = os.path.join(work_dir, 'allure.tar')
        runner_cmd += ['--allure', allure_path]
        outputs += [allure_path]

    propagate_test_timeout_info = False

    if is_for_distbuild:
        propagate_test_timeout_info = True
        # Write log to stderr to obtain it in case of the node timeout on distbuild(900s problem)
        runner_cmd += [
            "--log-level",
            "DEBUG",
            "--result-max-file-size",
            str(100 * 1024),
            "--disable-memory-snippet",
        ]
        if not getattr(opts, 'keep_full_test_logs', False):
            runner_cmd += ["--truncate-files"]
    else:
        runner_cmd += [
            "--result-max-file-size",
            "0",
        ]
    if opts.test_node_output_limit is not None:
        if "--truncate-files" not in runner_cmd:
            runner_cmd += ["--truncate-files"]
        runner_cmd += ["--truncate-files-limit", str(opts.test_node_output_limit)]

    if opts and opts.propagate_test_timeout_info:
        propagate_test_timeout_info = True

    if propagate_test_timeout_info:
        runner_cmd.append("--propagate-timeout-info")

    if in_canonize_mode(opts):
        runner_cmd += ["--dont-verify-results"]
    else:
        runner_cmd += ["--verify-results"]

    # Disable dumping test environment when ya:dirty is specified to avoid traversing entire arcadia
    if (
        opts.dump_test_environment or devtools.ya.test.const.YaTestTags.DumpTestEnvironment in suite.tags
    ) and devtools.ya.test.const.YaTestTags.Dirty not in suite.tags:
        runner_cmd += ["--dump-test-environment"]

    if devtools.ya.test.const.YaTestTags.DumpNodeEnvironment in suite.tags:
        runner_cmd += ["--dump-node-environment"]

    if devtools.ya.test.const.YaTestTags.CopyDataRO in suite.tags:
        runner_cmd += ["--data-to-environment", "copyro"]
    elif devtools.ya.test.const.YaTestTags.CopyData in suite.tags:
        runner_cmd += ["--data-to-environment", "copy"]

    if opts and getattr(opts, 'max_test_comment_size'):
        runner_cmd += [
            "--max-test-comment-size",
            str(opts.max_test_comment_size),
        ]

    if opts and getattr(opts, 'tests_limit_in_suite', 0):
        runner_cmd += [
            '--tests-limit-in-chunk',
            str(opts.tests_limit_in_suite),
        ]

    if opts and getattr(opts, 'test_keep_symlinks'):
        runner_cmd += [
            "--keep-symlinks",
        ]

    if inline_diff:
        runner_cmd += [
            "--max-inline-diff-size",
            "0",  # disable extracting diffs
            "--max-test-comment-size",
            "0",  # disable trimming test comments
        ]

    runner_cmd += ["--output-style", opts.output_style]

    python_bin = suite.get_python_bin(opts)
    if python_bin:
        runner_cmd += ["--python-bin", python_bin]

    python_lib = suite.get_python_library(opts)
    if python_lib:
        runner_cmd += ["--python-lib-path", python_lib]

    inputs.update(testdeps.get_suite_requested_input(suite, opts))

    for path in suite.python_paths:
        runner_cmd += ["--python-sys-path", path.replace("arcadia", '$(SOURCE_ROOT)')]
        inputs.add(path.replace("arcadia", '$(SOURCE_ROOT)'))

    if retry is not None:
        runner_cmd += ["--retry", str(retry)]

    if suite.test_run_cwd:
        runner_cmd += ["--test-run-cwd", suite.test_run_cwd]

    if suite.tags:
        for tag in suite.tags:
            runner_cmd += ["--tag", tag]

    if devtools.ya.test.const.YaTestTags.TraceOutput in suite.tags:
        tracelog = os.path.join(test_output_dir, "trace_output.log")
        runner_cmd += ["--trace-output-filename", tracelog]

    if suite.requires_ram_disk:
        runner_cmd += ["--requires-ram-disk"]

    runner_cmd.extend(_get_env_arg(opts, suite))

    if opts.setup_pythonpath_env and suite.setup_pythonpath_env:
        runner_cmd += ["--setup-pythonpath-env"]

    if suite.supports_canonization:
        runner_cmd.append("--supports-canonization")

    if suite.supports_test_parameters:
        runner_cmd.append("--supports-test-parameters")

    for key, value in (opts.test_params or {}).items():
        runner_cmd += ["--test-param={}={}".format(key, value)]

    if getattr(opts, 'test_allow_graceful_shutdown', True):
        if devtools.ya.test.const.YaTestTags.NoGracefulShutdown in suite.tags:
            # Use SIGQUIT to generate core dump immediately
            runner_cmd += ["--smooth-shutdown-signals", "SIGQUIT"]
        else:
            for sig in suite.smooth_shutdown_signals:
                runner_cmd += ["--smooth-shutdown-signals", sig]

    if split_test_factor > 1:
        runner_cmd += ["--split-count", str(split_test_factor), "--split-index", str(split_index)]

    if split_file:
        runner_cmd += ["--split-file", split_file]

    if opts.test_output_compression_filter:
        # XXX
        # We should change the name of the output archive from tar to tar.{filter},
        # but it's not possible currently - there are a lot of place where other systems are expecting testing_out_stuff.tar
        runner_cmd += [
            '--compression-filter',
            opts.test_output_compression_filter,
            '--compression-level',
            str(opts.test_output_compression_level),
        ]

    if custom_deps:
        deps += custom_deps

    # TODO DEVTOOLS-5416
    # We need to add macro like USE_GLOBAL_RESOURCE(name) which will declare
    # certain global resources required for testing to avoid passing all available resources.
    for resource in suite.get_global_resources():
        runner_cmd += ["--global-resource", resource]

    node_timeout = suite.timeout + devtools.ya.test.const.TEST_NODE_FINISHING_TIME

    if is_for_distbuild:
        runner_cmd += ["--node-timeout", str(node_timeout or devtools.ya.test.const.DEFAULT_TEST_NODE_TIMEOUT)]

    env = sysenv.get_common_env()
    sysenv.update_test_initial_env_vars(env, suite, opts)

    if platform_descriptor and 'llvm-symbolizer' in platform_descriptor.get("params", {}):
        # Set correct path to the symbolizer for every supported sanitizer
        symb_path = platform_descriptor["params"]["llvm-symbolizer"]
        env.update_mandatory(
            {
                "ASAN_SYMBOLIZER_PATH": symb_path,
                "LSAN_SYMBOLIZER_PATH": symb_path,
                "MSAN_SYMBOLIZER_PATH": symb_path,
                "TSAN_SYMBOLIZER_PATH": symb_path,
                "UBSAN_SYMBOLIZER_PATH": symb_path,
            }
        )

    env["YA_CXX"] = platform_descriptor.get("params", {}).get("cxx_compiler", "")
    env["YA_CC"] = platform_descriptor.get("params", {}).get("c_compiler", "")

    if opts and not getattr(opts, "random_ports"):
        env["NO_RANDOM_PORTS"] = "1"

    if not is_for_distbuild:
        env["TESTING_SAVE_OUTPUT"] = "yes"

    def add_docker_image_dep(image):
        image_node_uid = inject_download_docker_image_node(graph, image, opts)
        deps.append(image_node_uid)

    def add_sandbox_resource_dep(sandbox_resource):
        runner_cmd.extend(["--sandbox-resource", str(sandbox_resource)])
        resource_node_uid = sandbox_node.inject_download_sandbox_resource_node(
            graph, sandbox_resource, opts, suite.global_resources
        )
        deps.append(resource_node_uid)

    def add_external_file_resource_dep(node_name, file_path):
        src = os.path.join("$(SOURCE_ROOT)", file_path)
        dst = os.path.join("$(BUILD_ROOT)", "external_local", file_path)
        uid = "{}_{}".format(node_name, imprint.combine_imprints(src, dst))
        runner_cmd.extend(["--external-local-file", str(file_path)])

        if not graph.get_node_by_uid(uid):
            node = {
                "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
                'uid': uid,
                'broadcast': False,
                'cmds': [
                    {'cmd_args': ['$(PYTHON)/python', '$(SOURCE_ROOT)/build/scripts/fs_tools.py', 'copy', src, dst]}
                ],
                'deps': [],
                'inputs': [src],
                'kv': {'p': 'CP', 'pc': 'light-blue'},
                'outputs': [dst],
                'priority': 0,
                'env': {},
                'cache': False,
                "type": 2,
            }
            graph.append_node(node, add_to_result=False)
        deps.append(uid)

    if suite.recipes:
        runner_cmd += ['--recipes', suite.recipes]

    if suite.supports_canonization:
        # MDS canonical resources
        for resource in testdeps.get_test_mds_resources(suite):
            deps.extend(suite.mds_resource_deps)

    for resource in testdeps.get_test_sandbox_resources(suite):
        add_sandbox_resource_dep(resource)

    for resource in testdeps.get_test_ext_sbr_resources(suite, arc_root):
        add_sandbox_resource_dep(resource)

    for image in testdeps.get_docker_images(suite):
        add_docker_image_dep(image)

    for file_path in testdeps.get_test_ext_file_resources(suite, arc_root):
        abs_path = os.path.join(arc_root, file_path)
        _hash = imprint(abs_path)[abs_path]
        add_external_file_resource_dep(
            "external_local_resource_{}".format(_hash),
            file_path,
        )

    # don't download corpus if certain case is specified
    if suite.get_type() == fuzz_test.FUZZ_TEST_TYPE and not getattr(opts, "fuzz_case_filename", None):
        fuzz_automa = util_tools.get_corpus_data_path(suite.project_path, arc_root)
        if os.path.exists(fuzz_automa):
            with open(fuzz_automa) as afile:
                try:
                    corpus_data = json.load(afile)
                except ValueError as e:
                    logger.warning("Failed to load corpus %s: %s", fuzz_automa, str(e))
                    corpus_data = {}

            nparts = len(corpus_data.get("corpus_parts", []))
            if nparts > devtools.ya.test.const.MAX_CORPUS_RESOURCES_ALLOWED * 2:
                suite.corpus_parts_limit_exceeded = nparts

            if app_config.have_sandbox_fetcher:
                for field in corpus_data.keys():
                    for n, resource_id in enumerate(corpus_data[field]):
                        target_path = "{}/{}".format(field, n)
                        add_sandbox_resource_dep(sandbox_resource.Reference.create(resource_id, target_path))

    node_tag = None
    requirements = suite.default_requirements
    requirements.update(suite.requirements)

    if suite.test_size == devtools.ya.test.const.TestSize.Large:
        requirements["network"] = 'full'
        node_tag = "large"

    default_requirements = devtools.ya.test.const.TestSize.get_default_requirements(suite.test_size)

    default_ram_requirements = default_requirements.get(devtools.ya.test.const.TestRequirements.Ram)
    if requirements.get(devtools.ya.test.const.TestRequirements.Kvm):
        default_ram_requirements = devtools.ya.test.const.DEFAULT_RAM_REQUIREMENTS_FOR_KVM
        requirements["kvm"] = True
    suite_ram_requirements = requirements.get(devtools.ya.test.const.TestRequirements.Ram)
    requirements["ram"] = suite_ram_requirements or default_ram_requirements

    suite_cpu_requirements = requirements.get(devtools.ya.test.const.TestRequirements.Cpu)

    default_cpu_requirements = default_requirements.get(devtools.ya.test.const.TestRequirements.Cpu)
    # Disallow other nodes from execution while user will interact with debugger
    if getattr(opts, 'debugger_requested'):
        requirements["cpu"] = "all"
    elif suite_cpu_requirements:
        requirements["cpu"] = suite_cpu_requirements
    else:
        requirements["cpu"] = default_cpu_requirements

        # XXX Increase cpu requirement by default depending of ram requirement.
        # For more info see DISTBUILD-1313
        # Remove when DISTBUILD-329 is done.
        if suite_ram_requirements and opts.cpu_detect_via_ram:
            if suite_ram_requirements >= 12:
                requirements["cpu"] = max(4, default_cpu_requirements)
            elif suite_ram_requirements >= 8:
                requirements["cpu"] = max(3, default_cpu_requirements)
            elif suite_ram_requirements >= 4:
                requirements["cpu"] = max(2, default_cpu_requirements)

    if getattr(opts, 'use_throttling'):
        requirements["throttle_cpu"] = True

    suite_ram_disk_requirements = requirements.get(devtools.ya.test.const.TestRequirements.RamDisk)
    default_ram_disk_requirements = default_requirements.get(devtools.ya.test.const.TestRequirements.RamDisk)
    requirements["ram_disk"] = suite_ram_disk_requirements or default_ram_disk_requirements

    suite_network_requirements = requirements.get(devtools.ya.test.const.TestRequirements.Network)
    if suite_network_requirements:
        requirements["network"] = suite_network_requirements

    if devtools.ya.test.const.YaTestTags.HugeLogs in suite.tags:
        requirements["test_output_limit"] = 1024**3  # 1GiB

    ram_limit = requirements.get('ram', 0)
    if (
        devtools.ya.test.const.YaTestTags.NotAutocheck not in suite.tags
        and ram_limit
        and ram_limit != devtools.ya.test.const.TestRequirementsConstants.All
    ):
        runner_cmd += ["--ram-limit-gb", str(ram_limit)]

    if opts.private_ram_drive:
        runner_cmd += ['--local-ram-drive-size', str(requirements['ram_disk'])]

    kv = get_test_kv(
        suite, split_index=(split_index if split_test_factor > 1 else None), split_file=split_file, run_test_node=True
    )
    kv.update({('needs_resource' + r): True for r in suite.get_resources(opts)})

    if add_list_node(opts, suite):
        runner_cmd += ["--test-list-path", suite.work_dir(devtools.ya.test.const.TEST_LIST_FILE)]

    if suite.meta.canonize_sub_path:
        runner_cmd += ["--sub-path", suite.meta.canonize_sub_path]
    if suite.special_runner == 'yt' and opts.run_tagged_tests_on_yt:
        runner_cmd += ["--space-to-reserve", str(100 * 1024 * 1024)]
    tared_outputs = []
    dir_outputs_content = []
    stable_dir_outputs = False
    save_test_outputs = getattr(opts, 'save_test_outputs', True)
    dir_outputs = getattr(opts, 'dir_outputs', False)
    if save_test_outputs:
        testing_out_tar = os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_TAR_NAME)
        if dir_outputs:
            runner_cmd += ["--dir-outputs"]
            dir_outputs_content.append(os.path.join(work_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME))
            if _stable_dir_outputs(suite, opts):
                runner_cmd += ["--should-tar-dir-outputs"]
                stable_dir_outputs = True
        else:
            tared_outputs.append(testing_out_tar)

        if not dir_outputs or _stable_dir_outputs(suite, opts):
            runner_cmd += ["--tar", testing_out_tar]
            outputs.append(testing_out_tar)
    runner_cmd = runner_cmd + suite.get_run_cmd(opts, retry, is_for_distbuild)

    if opts.use_command_file_in_testtool:
        cmdline.wrap_test_tool_cmd(runner_cmd, 'run_test')

    node_cmds = [{"cmd_args": runner_cmd, "cwd": "$(BUILD_ROOT)"}]
    if suite.has_prepare_test_cmds():
        extra_cmds, extra_inputs = suite.get_prepare_test_cmds()
        inputs |= set(extra_inputs)
        node_cmds = extra_cmds + node_cmds

    if split_test_factor > 1 or retry is not None or suite.fork_test_files_requested(opts):
        uid_prefix = suite.uid.split("-")[0]
        uid = uid_gen.get_test_node_uid([suite.uid, split_index, retry, split_file], uid_prefix)
    else:
        uid = suite.uid

    node = {
        "backup": backup,
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "broadcast": False,
        "cache": _should_cache_suite(suite, opts),
        "cmds": node_cmds,
        "cwd": "$(BUILD_ROOT)",
        "deps": testdeps.unique(deps),
        "dir_outputs": dir_outputs_content,
        "stable_dir_outputs": stable_dir_outputs,
        "env": env.dump(),
        "inputs": testdeps.unique(inputs),
        "kv": kv,
        "node-type": devtools.ya.test.const.NodeType.TEST,
        "outputs": outputs,
        "priority": _get_suite_priority(suite),
        "requirements": requirements,
        "secrets": ['YA_COMMON_YT_TOKEN'],
        "tared_outputs": tared_outputs,
        "test-category": suite.get_ci_type_name(),
        "uid": uid,
    }

    if node_timeout:
        node["timeout"] = node_timeout

    if node_tag:
        node["tag"] = node_tag

    platform = platform_descriptor.get('platform_name', '').lower()
    if platform:
        node["platform"] = platform

    build_tags = [t for _, d in suite.get_build_deps() for t in d.get('tags')]
    if build_tags:
        node["tags"] = testdeps.unique(build_tags)

    return wrap_test_node(node, suite, work_dir, opts, platform_descriptor, split_index)


def get_test_kv(suite, **kwargs):
    kv = {
        "p": devtools.ya.test.const.TestSize.get_shorthand(suite.test_size),
        "pc": "yellow",
        "show_out": True,
        "path": os.path.join(suite.project_path, suite.name),
        "special_runner": suite.special_runner,
    }
    kv.update({k: v for k, v in kwargs.items() if v is not None})
    return kv


def get_simctl_path(platform_descriptor):
    return platform_descriptor['params']['simctl']


def get_profiles_path(platform_descriptor):
    return platform_descriptor['params']['profiles']


def wrap_test_node(node, suite, test_out_dir, opts, platform_descriptor, split_index):
    assert node['cmds']
    runner_cmd = node['cmds'][-1]

    if suite.special_runner == 'ios':
        node['cmds'] = make_ios_cmds(
            runner_cmd['cmd_args'],
            suite.uid,
            get_simctl_path(platform_descriptor),
            get_profiles_path(platform_descriptor),
            opts,
        )

    elif suite.special_runner.startswith('ios.simctl.'):
        runner_cmd['cmd_args'] += [
            '--ios-app',
            '--ios-simctl',
            get_simctl_path(platform_descriptor),
            '--ios-device-type',
            suite.get_ios_device_type(),
            '--ios-runtime',
            suite.get_ios_runtime(),
        ]
        if suite.special_runner[len('ios.simctl.') :] in ('x86_64', 'i386', 'arm64'):
            runner_cmd['cmd_args'] += ['--ios-profiles', get_profiles_path(platform_descriptor)]

    elif suite.special_runner.startswith('android.'):
        android_sdk_root = suite.global_resources.get(
            devtools.ya.test.const.ANDROID_SDK_ROOT, '$({})'.format(devtools.ya.test.const.ANDROID_SDK_ROOT)
        )
        android_avd_root = suite.global_resources.get(
            devtools.ya.test.const.ANDROID_AVD_ROOT, '$({})'.format(devtools.ya.test.const.ANDROID_AVD_ROOT)
        )
        runner_cmd['cmd_args'] += [
            '--android-app',
            '--android-sdk',
            android_sdk_root,
            '--android-avd',
            android_avd_root,
            '--android-arch',
            suite.special_runner[len('android.') :],
            '--android-activity',
            suite.get_android_apk_activity(),
        ]

    elif suite.special_runner == 'yt' and opts.run_tagged_tests_on_yt:
        # Wrap test node with YT test node runner
        inputs = []
        output_tar = os.path.join(test_out_dir, devtools.ya.test.const.YT_RUN_TEST_TAR_NAME)
        ytexec_tool = opts.ytexec_bin or "$(YTEXEC)/ytexec/ytexec"
        # yt_run_test steals args from run_test, no need to duplicate args
        ytexec_desc = "{project_path}({test_type}) - chunk{split_index}"
        description = ytexec_desc.format(
            project_path=suite.project_path, test_type=suite.get_type(), split_index=split_index
        )
        if opts.ytexec_title_suffix:
            description += " - {}".format(opts.ytexec_title_suffix)
        wrapper_cmd = util_tools.get_test_tool_cmd(opts, "ytexec_run_test", suite.global_resources) + [
            '--output-tar',
            output_tar,
            '--description',
            description,
            '--ytexec-tool',
            ytexec_tool,
            '--cpu',
            str(node['requirements']['cpu']),
            '--network',
            node['requirements']['network'],
            '--ram-disk',
            str(node['requirements']['ram_disk']),
            '--ram',
            str(node['requirements']['ram']),
        ]

        # pass it as env variable because we hash command in static_uid
        node['env']['TEST_NODE_SUITE_UID'] = node['uid']

        for o in testdeps.unique(node["outputs"] + [output_tar]):
            wrapper_cmd += ["--ytexec-outputs", o]

        if opts.ytexec_node_timeout:
            wrapper_cmd += ["--ytexec-node-timeout", str(opts.ytexec_node_timeout)]

        if opts.dir_outputs:
            for o in node["dir_outputs"]:
                wrapper_cmd += ["--ytexec-outputs", o]

        if opts.vanilla_execute_yt_token_path:
            wrapper_cmd += ['--yt-token-path', opts.vanilla_execute_yt_token_path]

        for filename in suite.yt_spec_files:
            filename = '$(SOURCE_ROOT)/' + filename
            wrapper_cmd += ['--yt-spec-file', filename]
            inputs.append(filename)

        runner_cmd['cmd_args'] = wrapper_cmd + runner_cmd['cmd_args']

        node["kv"]["p"] = "YT"

        # TODO DEVTOOLS-5416
        # Transfer requirements from node to the YT operation
        node.update(
            {
                "inputs": testdeps.unique(node["inputs"] + inputs),
                "outputs": testdeps.unique(node["outputs"] + [output_tar]),
                "tared_outputs": testdeps.unique(node["tared_outputs"] + [output_tar]),
                # Drop requirements, because test node itself will be launched over the YT
                "requirements": {
                    # cpu is required for compression and uploading
                    "cpu": "{}m".format(opts.ytexec_wrapper_m_cpu),
                    "network": "full",
                    "ram": 8,
                },
            }
        )

    return node


def create_sandbox_run_test_node(orig_node, suite, nodes_map, frepkage_res_info, deps, opts):
    def get_test_node(node):
        for _ in range(10):
            if node['kv'].get('run_test_node'):
                return node
            node = nodes_map[node['deps'][0]]
        raise AssertionError(node)

    out_dir = suite.work_dir()
    output_tar = os.path.join(out_dir, 'sandbox_run_test.tar')
    outputs_map = {os.path.basename(x): x for x in orig_node["outputs"]}

    test_node = get_test_node(orig_node)

    runner_cmd = util_tools.get_test_tool_cmd(opts, "sandbox_run_test", suite.global_resources) + [
        '--output-tar',
        output_tar,
        # Get requirements from run_test node with real requirements (results_accumulator and results_merger won't have them)
        '--requirements',
        json.dumps(test_node.get('requirements', {})),
        '--frepkage-res-info',
        frepkage_res_info,
    ]

    for filename in orig_node["outputs"]:
        runner_cmd += ['--empty-output', filename]

    if opts.download_artifacts:
        runner_cmd += ["--download-artifacts"]
    stable_dir_outputs = False
    if opts.dir_outputs:
        runner_cmd += ["--dir-outputs"]
        if _stable_dir_outputs(suite, opts):
            runner_cmd += ["--should-tar-dir-outputs"]
            stable_dir_outputs = True
    if opts.custom_fetcher:
        runner_cmd += ["--task-custom-fetcher", opts.custom_fetcher]
    if opts.resource_owner:
        runner_cmd += ['--task-sandbox-owner', opts.resource_owner]
    if opts.oauth_token:
        runner_cmd += ['--task-sandbox-token', opts.oauth_token]

    # All options added to the runner_cmd below this comment will be stolen from run_test handler
    runner_cmd += [
        '--meta',
        outputs_map['meta.json'],
        '--log-path',
        outputs_map['run_test.log'],
        '--trace',
        outputs_map[devtools.ya.test.const.TRACE_FILE_NAME],
        '--build-root',
        '$(BUILD_ROOT)',
        '--test-suite-name',
        suite.name,
        '--project-path',
        suite.project_path,
        '--test-size',
        suite.test_size,
        '--timeout',
        str(suite.timeout),
        '--test-ci-type',
        suite.get_ci_type_name(),
        '--test-type',
        suite.get_type(),
    ]

    if suite.target_platform_descriptor:
        runner_cmd += ['--target-platform-descriptor', suite.target_platform_descriptor]
    if suite.multi_target_platform_run:
        runner_cmd.append('--multi-target-platform-run')
    for tag in suite.tags:
        runner_cmd += ['--tag', tag]
    timeout = test_node.get('timeout')
    if timeout:
        runner_cmd += ['--node-timeout', str(timeout)]

    env = test_node["env"].copy()
    # pass it as env variable because we hash command in static_uid
    env['TEST_NODE_SUITE_UID'] = str(suite.uid)
    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST,
        "backup": False,
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "broadcast": False,
        "inputs": [frepkage_res_info],
        "uid": suite.uid,
        "cache": False,
        "cwd": "$(BUILD_ROOT)",
        "priority": _get_suite_priority(suite),
        # Drop original deps - YA_MAKE sandbox task will built it
        "deps": testdeps.unique(deps),
        # FS stands for ya:force_sandbox
        "kv": get_test_kv(suite, p="FS"),
        "env": env,
        "outputs": orig_node["outputs"] + [output_tar],
        "tared_outputs": orig_node["tared_outputs"] + [output_tar],
        "dir_outputs": orig_node["dir_outputs"],
        "stable_dir_outputs": stable_dir_outputs,
        # Drop original requirements, test node will be launched in the Sandbox
        "requirements": {
            "cpu": 1,
            "network": "full",
            "ram": 4,
        },
        "cmds": [
            {
                "cmd_args": runner_cmd,
                "cwd": "$(BUILD_ROOT)",
            }
        ],
    }

    if timeout:
        node['timeout'] = timeout

    return node


def make_ios_cmds(origin_cmd, uid, simctl_path, profiles_path, opts):
    temp_device_name = "devtools-" + (uid or uid_gen.get_random_uid())
    runner_script = '$(SOURCE_ROOT)/build/scripts/run_ios_simulator.py'
    spawn_cmd = test_common.get_python_cmd(opts) + [
        runner_script,
        "--action",
        "spawn",
        "--simctl",
        simctl_path,
        "--profiles",
        profiles_path,
        "--device-dir",
        "$(BUILD_ROOT)",
        "--device-name",
        temp_device_name,
    ]
    return [
        wrap_cmd(
            test_common.get_python_cmd(opts)
            + [
                runner_script,
                "--action",
                "create",
                "--simctl",
                simctl_path,
                "--profiles",
                profiles_path,
                "--device-dir",
                "$(BUILD_ROOT)",
                "--device-name",
                temp_device_name,
            ]
        ),
        wrap_cmd(spawn_cmd + origin_cmd),
    ]


def _get_suite_priority(suite):
    if suite.test_size in devtools.ya.test.const.TestSize.DefaultPriorities:
        return devtools.ya.test.const.TestSize.get_default_priorities(suite.test_size)
    return 0


def _should_cache_suite(suite, opts):
    return (opts.cache_tests or suite.cache_test_results) and not opts.force_retest and not opts.test_disable_timeout


def _need_random_uid(suite, opts):
    return not _should_cache_suite(suite, opts) or in_canonize_mode(opts)


def _stable_dir_outputs(suite, opts):
    return not _need_random_uid(suite, opts) and opts.dir_outputs_test_mode


def get_suite_uid(
    suite,
    graph,
    arc_root,
    opts,
    is_for_distbuild,
    out_dir,
):
    if _need_random_uid(suite, opts):
        uid = uid_gen.get_random_uid()
    else:
        # XXX required redesign
        uid_changing_opts = (
            'detect_leaks_in_pytest',
            'dir_outputs',
            'dir_outputs_test_mode',
            'fast_clang_coverage_merge',
            'keep_temps',
            'save_test_outputs',
            'test_output_compression_filter',
            'test_output_compression_level',
            'go_coverage',
            'use_throttling',
            'test_prepare',
            'test_node_output_limit',
            'tests_limit_in_suite',
        )

        imprint_parts = (
            suite.get_run_cmd(opts, retry=None, for_dist_build=is_for_distbuild)
            + [uid_gen.TestUidGenerator.get(suite, graph, arc_root, opts)]
            + ["{}={}".format(x, getattr(opts, x)) for x in uid_changing_opts if getattr(opts, x, None)]
        )
        # XXX
        # Suite output paths may contain the name of the target platform,
        # which can lead to problems when different platforms will not affect test's uid.
        # When result is cached for the one platform it will be placed in a directory with its name.
        # But in a graph for another platform containing a test with the same uid,
        # results will not be found because it will be expected in a different directory.
        # Current issue: java tests have the same uids for the default linux platform and musl.
        imprint_parts.append(out_dir)
        imprint_parts.append(opts.flags.get('FAKEID', ''))
        # Allows to change test's uid per suite type
        imprint_parts.append(str(opts.test_types_fakeid.get(suite.get_type(), '')))
        imprint_parts.append(str(opts.test_fakeid))
        imprint_parts.append(str(opts.tests_retries))
        imprint_parts.append(str(suite.fork_test_files_requested(opts)))
        imprint_parts.append(str(suite.get_fork_partition_mode()))

        uid = '-'.join(map(str, [_f for _f in ['test', imprint.combine_imprints(*imprint_parts)] if _f]))
    return uid


def get_node_out_dirs(node):
    dirs = [
        os.path.dirname(filename)
        for filename in node["outputs"]
        if os.path.basename(filename) == devtools.ya.test.const.TRACE_FILE_NAME
    ]
    assert dirs, "Failed to find node output dirs: {}".format(node["outputs"])
    return dirs


def get_test_node_out_dir(node):
    dirs = get_node_out_dirs(node)
    assert (
        len(dirs) == 1
    ), "Found more then one output dir. Looks like it's not a test node, but test aggregation node: {}".format(node)
    return dirs[0]


def get_test_node_rel_output(node, field, skip_filenames=None):
    test_node_output_dir = get_test_node_out_dir(node)
    files = [os.path.relpath(filename, test_node_output_dir) for filename in node[field]]
    if skip_filenames:
        return [filename for filename in files if filename not in skip_filenames]
    return files


def intermediate_test_nodes(test_nodes):
    # Intermediate test nodes shouldn't have tared_output - it will be provided by merger
    for node in test_nodes:
        node['tared_outputs'] = []


def create_results_accumulator_node(test_nodes, suite, graph, retry, opts=None, backup=False):
    test_uids = [node["uid"] for node in test_nodes]
    if retry is not None:
        uid = suite.uid + "-run{}".format(retry)
    else:
        uid = suite.uid
    out_dir = test_common.get_test_suite_work_dir(
        "$(BUILD_ROOT)",
        suite.project_path,
        suite.name,
        retry,
        target_platform_descriptor=suite.target_platform_descriptor,
        multi_target_platform_run=suite.multi_target_platform_run,
        remove_tos=opts.remove_tos,
    )

    if opts.merge_split_tests:
        skip_outputs = None
    else:
        skip_outputs = [devtools.ya.test.const.YT_RUN_TEST_TAR_NAME]
        if opts.dir_outputs:
            skip_outputs.append(devtools.ya.test.const.TESTING_OUT_DIR_NAME)
            if _stable_dir_outputs(suite, opts):
                skip_outputs.append(devtools.ya.test.const.TESTING_OUT_TAR_NAME)
        else:
            skip_outputs.append(devtools.ya.test.const.TESTING_OUT_TAR_NAME)

    # test_nodes has the same outputs, so we can take any node as source of output samples
    test_node = test_nodes[0]
    outputs = [
        os.path.join(out_dir, filename) for filename in get_test_node_rel_output(test_node, "outputs", skip_outputs)
    ]
    tared_outputs = [
        os.path.join(out_dir, filename)
        for filename in get_test_node_rel_output(test_node, "tared_outputs", skip_outputs)
    ]
    dir_outputs = [
        os.path.join(out_dir, filename) for filename in get_test_node_rel_output(test_node, "dir_outputs", skip_outputs)
    ]
    node_inputs = [filename for node in test_nodes for filename in node["outputs"]]
    node_log_path = os.path.join(out_dir, "results_accumulator.log")
    cmd = util_tools.get_test_tool_cmd(opts, "results_accumulator", suite.global_resources) + [
        "--accumulator-path",
        out_dir,
        "--source-root",
        "$(SOURCE_ROOT)",
        "--log-path",
        node_log_path,
        "--concatenate-binaries",
        "report.exec",
        # yt_run_test log
        "--concatenate-binaries",
        "operation.log",
        # "--log-level", "DEBUG"
    ]

    if opts.use_distbuild:
        cmd += [
            "--truncate",
            "--log-level",
            "DEBUG",
        ]

    # XXX
    if not exts.windows.on_win():
        cmd += ["--gdb-path", "$(GDB)/gdb/bin/gdb"]

    if opts.keep_temps:
        cmd += ["--keep-temps"]

    if opts.tests_limit_in_suite:
        cmd += [
            '--tests-limit-in-suite',
            str(opts.tests_limit_in_suite),
        ]

    if opts.keep_temps or not opts.merge_split_tests:
        cmd += ["--keep-paths"]

    for node in test_nodes:
        output_path = os.path.dirname(node["outputs"][0])
        cmd += ["--output", output_path]
    if opts.save_test_outputs and not opts.merge_split_tests:
        for node in test_nodes:
            if not opts.dir_outputs or _stable_dir_outputs(suite, opts):
                filename = os.path.join(get_test_node_out_dir(node), devtools.ya.test.const.TESTING_OUT_TAR_NAME)
                outputs.append(filename)
                if not opts.dir_outputs:
                    tared_outputs.append(filename)
            if opts.dir_outputs:
                filename = os.path.join(get_test_node_out_dir(node), devtools.ya.test.const.TESTING_OUT_DIR_NAME)
                dir_outputs.append(filename)
            yt_run_test_filename = os.path.join(
                get_test_node_out_dir(node), devtools.ya.test.const.YT_RUN_TEST_TAR_NAME
            )
            if yt_run_test_filename in node["outputs"]:
                outputs.append(yt_run_test_filename)
                tared_outputs.append(yt_run_test_filename)

    cov_inputs = {
        'coverage.tar': [],
        'java.coverage.tar': [],
        'py2.coverage.tar': [],
        'py3.coverage.tar': [],
    }
    should_skip = {
        'coverage.tar': False,
        'py3.coverage.tar': getattr(opts, 'python_coverage', False),
    }
    files_to_skip = set()
    dirs_to_skip = set()
    if not opts.merge_split_tests:
        if not opts.dir_outputs or _stable_dir_outputs(suite, opts):
            files_to_skip = {devtools.ya.test.const.TESTING_OUT_TAR_NAME}
        if opts.dir_outputs:
            dirs_to_skip = {devtools.ya.test.const.TESTING_OUT_DIR_NAME}
        files_to_skip.add(devtools.ya.test.const.YT_RUN_TEST_TAR_NAME)
    for filename in node_inputs:
        basename = os.path.basename(filename)
        if basename in cov_inputs:
            cov_inputs[basename].append(filename)
            # Will me merged with another tool in current cmds later
            if should_skip.get(basename):
                files_to_skip.add(basename)

    for filename in sorted(files_to_skip):
        cmd += ['--skip-file', filename]

    for filename in sorted(dirs_to_skip):
        cmd += ['--skip-dir', filename]

    if opts and getattr(opts, "fast_clang_coverage_merge"):
        log_path = os.path.join(out_dir, 'coverage_merge_res_accumulator.log')
        cmd += ["--fast-clang-coverage-merge", log_path]
        outputs += [log_path]

    cmds = [{'cmd_args': cmd, "cwd": "$(BUILD_ROOT)"}]

    if getattr(opts, 'python_coverage', False):
        # TODO move to the suite method, get rid of graph
        pydeps = False
        if opts.python_coverage:
            for u, output in coverage.rigel.get_suite_binary_deps(suite, graph):
                pydeps |= graph.is_target_python3(u)

        if pydeps:
            filename = "py3.coverage.tar"
            output_path = os.path.join(out_dir, filename)
            cmd = util_tools.get_test_tool_cmd(opts, "merge_python_coverage", suite.global_resources) + [
                "--output",
                output_path,
                "--name-filter",
                ":py3:cov",
            ]
            for dirname in cov_inputs[filename]:
                cmd += ["--coverage-path", dirname]
            cmds.append({'cmd_args': cmd, "cwd": "$(BUILD_ROOT)"})

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST,
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "cache": _should_cache_suite(suite, opts),
        "backup": backup,
        "broadcast": False,
        "inputs": testdeps.unique(node_inputs),
        "uid": uid,
        "priority": _get_suite_priority(suite),
        "deps": testdeps.unique(test_uids),
        "env": sysenv.get_common_env().dump(),
        "outputs": testdeps.unique(outputs) + [node_log_path],
        "tared_outputs": testdeps.unique(tared_outputs),
        "dir_outputs": testdeps.unique(dir_outputs),
        'kv': {
            "p": "TA",
            "pc": 'light-cyan',
            "show_out": True,
        },
        "requirements": gen_plan.get_requirements(opts, {"network": "restricted"}),
        "cmds": cmds,
    }

    intermediate_test_nodes(test_nodes)

    if opts and getattr(opts, 'clang_coverage', False):
        node["timeout"] = 1500
        if getattr(opts, 'fast_clang_coverage_merge', False):
            # merge_clang_coverage_archives_using_vfs from the results_accumulator.py
            # spawns 3 processes to extract data and fuse vfs
            node["requirements"] = {
                "cpu": 4,
                "network": "restricted",
            }

    return node


def get_merger_root_path(suites):
    assert suites
    return os.path.join(suites[0].project_path, "tests_merger")


def create_merge_test_runs_node(graph, test_nodes, suite, opts, backup, upload_to_remote_store):
    test_uids = [node["uid"] for node in test_nodes]
    uid = suite.uid
    out_dir = suite.work_dir()

    node_log_path = os.path.join(out_dir, "results_merge.log")
    node_inputs = [filename for node in test_nodes for filename in node["outputs"]]
    if opts.dir_outputs:
        node_inputs += [filename for node in test_nodes for filename in node["dir_outputs"]]

    outputs = {node_log_path}
    tared_outputs = set()
    dir_outputs = set()
    for test_node in test_nodes:
        for filename in test_node["outputs"]:
            basename = os.path.basename(filename)
            if basename in devtools.ya.test.const.TEST_NODE_OUTPUT_RESULTS:
                outputs.add(filename)
                tared_outputs.add(filename)
            else:
                outputs.add(os.path.join(out_dir, basename))
        if opts.dir_outputs:
            for filename in test_node['dir_outputs']:
                dir_outputs.add(filename)
    cmd = util_tools.get_test_tool_cmd(opts, "results_merger", suite.global_resources) + [
        "--project-path",
        suite.project_path,
        "--suite-name",
        suite.name,
        "--source-root",
        "$(SOURCE_ROOT)",
        "--log-path",
        node_log_path,
        # "--log-level", "DEBUG"
    ]

    if opts.keep_temps:
        cmd += ["--keep-temps"]

    if opts.remove_tos:
        cmd += ["--remove-tos"]

    if suite.target_platform_descriptor:
        cmd += ["--target-platform-descriptor", suite.target_platform_descriptor]

    if suite.multi_target_platform_run:
        cmd += ["--multi-target-platform-run"]

    for i in range(len(test_nodes)):
        work_dir = test_common.get_test_suite_work_dir(
            "$(BUILD_ROOT)",
            suite.project_path,
            suite.name,
            i + 1,
            target_platform_descriptor=suite.target_platform_descriptor,
            multi_target_platform_run=suite.multi_target_platform_run,
            remove_tos=opts.remove_tos,
        )
        cmd += ["--output", work_dir]

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST,
        "cache": _should_cache_suite(suite, opts),
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "backup": backup,
        "broadcast": False,
        "upload": upload_to_remote_store,
        "inputs": testdeps.unique(node_inputs),
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": _get_suite_priority(suite),
        "deps": testdeps.unique(test_uids),
        "env": sysenv.get_common_env().dump(),
        "outputs": testdeps.unique(outputs),
        "tared_outputs": testdeps.unique(tared_outputs),
        "dir_outputs": testdeps.unique(dir_outputs),
        'kv': {
            "p": "TR",
            "pc": 'light-cyan',
            "show_out": True,
        },
        "cmds": [{"cmd_args": cmd, "cwd": "$(BUILD_ROOT)"}],
    }

    intermediate_test_nodes(test_nodes)

    graph.append_node(node, add_to_result=True)
    return node


def inject_single_test_node(arc_root, graph, suite, custom_deps, opts, platform_descriptor):
    test_nodes = []
    tests_retries = (
        1
        if (devtools.ya.test.const.YaTestTags.Noretries in suite.tags or not suite.support_retries())
        else opts.tests_retries
    )
    if opts.tests_retries > 1 and devtools.ya.test.const.YaTestTags.Noretries in suite.tags:
        logger.warning(
            "{} is tagged with {} and will be scheduled for execution only once despite --test-retires={}".format(
                suite, devtools.ya.test.const.YaTestTags.Noretries, opts.tests_retries
            )
        )
    for i in range(tests_retries):
        retry = i + 1 if tests_retries > 1 else None
        node = create_test_node(
            arc_root,
            suite,
            graph,
            platform_descriptor,
            custom_deps=copy.copy(custom_deps),
            opts=opts,
            retry=retry,
        )

        add_to_result = tests_retries < 2

        if opts is not None and getattr(opts, 'upload_to_remote_store', False):  # TODO YA-316
            node['upload'] = add_to_result

        graph.append_node(node, add_to_result=add_to_result)
        test_nodes.append(node)
    return test_nodes


def inject_split_test_nodes(arc_root, graph, suite, custom_deps, opts, platform_descriptor):
    # type: (tp.Any, tp.Any, Suite, tp.Any, tp.Any, tp.Any) -> tp.Any
    acc_nodes = []
    tests_retries = 1 if devtools.ya.test.const.YaTestTags.Noretries in suite.tags else opts.tests_retries
    if suite.fork_test_files_requested(opts) and opts.test_files_filter:
        suite.save_old_canondata = True
    for i in range(tests_retries):
        retry = i + 1 if tests_retries > 1 else None
        test_nodes = []

        # We need to specify some info about chunks to generate test nodes properly.
        # This code assumes that suite won't be changed in the create_test_node method.
        # Store original data to avoid copying of the suite.
        orig_split_params = suite.get_split_params()
        for chunk in suite.chunks:
            suite.set_split_params(chunk.nchunks, chunk.chunk_index, chunk.filename)
            test_node = create_test_node(
                arc_root,
                suite,
                graph,
                platform_descriptor,
                custom_deps=copy.copy(custom_deps),
                opts=opts,
                retry=retry,
                split_test_factor=chunk.nchunks,
                split_index=chunk.chunk_index,
                split_file=chunk.filename,
            )
            for dep_uid, build_dep in suite.get_build_deps():
                suite.add_build_dep(build_dep["project_path"], build_dep["platform"], dep_uid, build_dep["tags"])
            test_nodes.append(test_node)

        suite.set_split_params(*orig_split_params)

        for test_node in test_nodes:
            graph.append_node(test_node, add_to_result=False)
        acc_node = create_results_accumulator_node(
            test_nodes, suite, graph, retry, opts=opts, backup=getattr(opts, 'backup_test_results', False)
        )

        add_to_result = tests_retries < 2
        if opts is not None and getattr(opts, 'upload_to_remote_store', False):  # TODO YA-316
            acc_node['upload'] = add_to_result

        acc_nodes.append(acc_node)
        graph.append_node(acc_node, add_to_result=add_to_result)

    return acc_nodes


# stub for now, seek a more intelligent solution later
def make_test_merge_chunks(tests):
    chunks = collections.defaultdict(list)
    for suite in tests:
        chunks[suite.project_path].append(suite)
    return chunks.values()


def filter_last_failed(tests, opts):
    store_path = last_failed.get_tests_restart_cache_dir(opts.bld_dir)
    status_storage = last_failed.StatusStore(store_path)
    is_all_empty = True
    result_tests = []
    for suite in tests:
        config_test_hash = suite.get_state_hash()
        content = status_storage.get(config_test_hash)
        if content is None:
            continue
        if content.keys():
            is_all_empty = False
            suite.insert_additional_filters([ytest_common_tools.to_utf8(k) for k in content.keys()])
            result_tests.append(suite)
    status_storage.flush()
    if is_all_empty or opts.tests_filters:
        return tests
    else:
        return result_tests


def configure_suites(suites, process, stage, add_error_func):
    def _p(data, p):
        return data[max(int(round(p * len(data))) - 1, 0)][1]

    times = []
    processed = []

    for suite in suites:
        start = time.time()
        with test_error.SuiteCtx(add_error_func, suite):
            try:
                process(suite)
            except Exception as e:
                test_error.SuiteCtx.add_error(e)
            else:
                processed.append(suite)

        times.append((suite, time.time() - start))

    if times:
        times.sort(key=lambda x: x[1])
        logger.debug(
            "Processed %d suite(s) in %s stage: p100:%0.4fs p99:%0.4fs p95:%0.4fs",
            len(times),
            stage,
            _p(times, 1.0),
            _p(times, 0.99),
            _p(times, 0.95),
        )
        logger.debug(
            "The longest 10 suites processed:\n%s",
            '\n'.join("{:.4f}s {} chunks:{}".format(t, s, len(s.chunks)) for s, t in reversed(times[-10:])),
        )

    return processed


def inject_test_nodes(arc_root, graph, tests, platform_descriptor, custom_deps=None, opts=None):
    injected_tests = []

    def process(suite):
        assert not suite.is_skipped(), suite
        if opts.tests_chunk_filters:
            suite.chunks = [
                x for x in suite.chunks if test_filter.make_name_filter(opts.tests_chunk_filters)(x.get_name())
            ]
        # If suite doesn't contain any chunk to be launched - it shouldn't be prepared to be run
        assert suite.chunks, suite

        split_test_factor = suite.get_split_factor(opts) if suite.support_splitting(opts) else 1

        test_deps = custom_deps
        if add_list_node(opts, suite):
            list_test_node = inject_test_list_node(
                arc_root, graph, suite, opts, custom_deps, platform_descriptor, modulo=split_test_factor
            )
            list_node_uid = list_test_node['uid']
            test_deps = custom_deps + [list_node_uid]
            graph.append_node(list_test_node, add_to_result=True)

        if opts.test_prepare:
            split_test_factor = 1
            split_test_nodes = False
        else:
            split_test_factor = suite.get_split_factor(opts) if suite.support_splitting(opts) else 1
            split_test_nodes = bool(split_test_factor > 1 or suite.fork_test_files_requested(opts))

        try:
            if split_test_nodes:
                test_nodes = inject_split_test_nodes(
                    arc_root,
                    graph,
                    suite,
                    test_deps,
                    opts,
                    platform_descriptor,
                )
            else:
                test_nodes = inject_single_test_node(
                    arc_root,
                    graph,
                    suite,
                    test_deps,
                    opts,
                    platform_descriptor,
                )
        except _DependencyException:
            logger.warning("Skipping test '%s (%s)' due to unresolved dependencies", suite.project_path, suite.name)
            return

        injected_tests.append(suite)

        assert test_nodes
        if len(test_nodes) > 1:
            backup = opts is not None and getattr(opts, 'backup_test_results', False)
            upload_to_remote_store = opts is not None and getattr(opts, 'upload_to_remote_store', False)
            suite_run_node = create_merge_test_runs_node(graph, test_nodes, suite, opts, backup, upload_to_remote_store)
        else:
            suite_run_node = test_nodes[0]

        suite._result_uids = [suite_run_node["uid"]]
        suite._output_uids = [n["uid"] for n in test_nodes]
        suite.dep_uids = suite_run_node["deps"]

        if suite.special_runner == 'sandbox' and opts.run_tagged_tests_on_sandbox:
            # For more info see https://st.yandex-team.ru/DEVTOOLS-5990#5dfa5ff7a0fc417baad0fc56
            if devtools.ya.test.const.YaTestTags.Dirty in suite.tags:
                logger.info(
                    "%s won't be launched on Sandbox due specified tag ya:dirty (--run-tagged-tests-on-sandbox doesn't support it)",
                    suite,
                )
            else:
                ctx = graph.get_context()
                if 'sandbox_run_test_result_uids' not in ctx:
                    ctx['sandbox_run_test_result_uids'] = [suite.uid]
                else:
                    ctx['sandbox_run_test_result_uids'].append(suite.uid)

        elif opts.use_distbuild and devtools.ya.test.const.YaTestTags.Dirty in suite.tags:
            logger.info(
                "%s might work incorrectly on Distbuild due specified tag ya:dirty (--dist mode doesn't support it)",
                suite,
            )

    configure_suites(tests, process, 'suites-injection', add_error_func=None)

    return injected_tests


def in_canonize_mode(test_opts):
    if getattr(test_opts, "canonize_tests", False):
        if test_opts.use_distbuild:
            raise devtools.ya.core.yarg.FlagNotSupportedException("Cannon perform test canonization on distbuild")
        if getattr(test_opts, "tests_retries", 1) > 1:
            raise devtools.ya.core.yarg.FlagNotSupportedException(
                "Cannon perform test canonization on multiple test runs"
            )
        return True
    return False


def in_fuzzing_mode(test_opts):  # XXX
    if getattr(test_opts, "fuzzing", False):
        if test_opts.use_distbuild:
            raise devtools.ya.core.yarg.FlagNotSupportedException("Fuzzing is currently not supported on distbuild")
        return True
    return False


def inject_tests(arc_root, plan, suites, test_opts, platform_descriptor):
    if not suites:
        return []

    timer = exts.timer.Timer('inject_tests')

    custom_deps = []
    if getattr(test_opts, 'checkout', False) and not getattr(test_opts, 'checkout_data_by_ya', False):
        if test_opts.use_distbuild:
            raise devtools.ya.core.yarg.FlagNotSupportedException(
                "--dist & -t & --checkout are not supported together, you can try --checkout --checkout-by-ya"
            )
        custom_deps.extend(inject_test_checkout_node(plan, suites, arc_root, opts=test_opts))
        timer.show_step('inject test checkout nodes')

    if getattr(test_opts, "list_tests", False):
        injected_tests = inject_test_list_nodes(arc_root, plan, suites, test_opts, custom_deps, platform_descriptor)
        timer.show_step('inject test list nodes')
    else:
        injected_tests = inject_test_nodes(arc_root, plan, suites, platform_descriptor, custom_deps, opts=test_opts)
        if in_canonize_mode(test_opts):
            canonization_nodes = []
            for suite in suites:
                if suite.supports_canonization:
                    canonization_nodes.append(
                        _inject_canonize_node(
                            plan,
                            suite,
                            test_opts.sandbox_url,
                            test_opts.resource_owner,
                            test_opts.ssh_keys,
                            test_opts.username,
                            test_opts.canonization_transport,
                            test_opts,
                        )
                    )
            if canonization_nodes:
                inject_canonization_result_node(
                    [s for s in suites if s.supports_canonization], plan, canonization_nodes, test_opts
                )
                timer.show_step('inject canonize nodes')
            else:
                logger.warning("No tests suitable for canonization found")
                logger.debug("Found test suites: %s", suites)
        elif in_fuzzing_mode(test_opts):
            fuzzing.inject_fuzz_postprocess_nodes(arc_root, plan, suites, test_opts)
            timer.show_step('inject fuzz postprocess nodes')

    # XXX
    if test_opts.use_distbuild:
        if plan.get_context().get('sandbox_run_test_result_uids'):
            raise devtools.ya.core.yarg.FlagNotSupportedException(
                "--run-tagged-tests-on-sandbox and --dist are not compatible"
            )

        if getattr(test_opts, 'test_diff', False):
            raise devtools.ya.core.yarg.FlagNotSupportedException("--canon-diff and --dist are not compatible")

    if (
        getattr(test_opts, 'checkout', False)
        and not getattr(test_opts, 'checkout_data_by_ya', False)
        and plan.get_context().get('sandbox_run_test_result_uids')
    ):
        raise devtools.ya.core.yarg.FlagNotSupportedException(
            "--run-tagged-tests-on-sandbox and --checkout are not supported together by default, you can try --checkout --checkout-by-ya"
        )

    timer.show_step('inject test nodes')

    cov_suites = [x for x in suites if x.supports_coverage]
    if len(cov_suites):
        coverage.inject_coverage_nodes(arc_root, plan, cov_suites, test_opts, platform_descriptor)
        timer.show_step('inject coverage merge nodes')

    return injected_tests


def create_populate_token_to_sandbox_vault_node(global_resources, opts):
    node_log_path = "$(BUILD_ROOT)/populate_token_to_sandbox_vault.log"

    node_cmd = util_tools.get_test_tool_cmd(opts, "populate_token_to_sandbox_vault", global_resources) + [
        '--log-path',
        node_log_path,
        '--secret-name',
        devtools.ya.test.const.SANDBOX_RUN_TEST_YT_TOKEN_VALUE_NAME,
    ]

    return {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": [],
        "uid": uid_gen.get_random_uid("populate_token_2_sb_vault"),
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": [],
        "cache": False,
        "target_properties": {},
        "outputs": [node_log_path],
        "kv": {
            "p": "UL",
            "pc": "dark-cyan",
            "show_out": True,
        },
        "requirements": gen_plan.get_requirements(opts, {"network": "full"}),
        "cmds": [{"cmd_args": node_cmd, "cwd": "$(BUILD_ROOT)"}],
    }


def create_upload_frepkage_node(filename, global_resources, opts):
    node_log_path = "$(BUILD_ROOT)/frepkage_upload.log"
    node_output = "$(BUILD_ROOT)/frepkage_upload.json"
    hostname = socket.getfqdn()
    username = getpass.getuser()

    node_cmd = util_tools.get_test_tool_cmd(opts, "upload", global_resources) + [
        "--target",
        filename,
        "--output",
        node_output,
        "--sandbox",
        opts.sandbox_url,
        "--type",
        "FROZEN_REPOSITORY_PACKAGE",
        "--description",
        "Generated frozen repository project (frepkage)\nHostname:{}\nUsername:{}".format(hostname, username),
        "--meta-info",
        json.dumps({"hostname": hostname, "username": username}),
        "--resource-ttl",
        str(3),
        "--log-path",
        node_log_path,
    ]

    if opts.resource_owner:
        node_cmd += ["--owner", opts.resource_owner]
    node_cmd += util_shared.get_oauth_token_options(opts)
    if opts.ssh_keys:
        for i in opts.ssh_keys:
            node_cmd += ["--ssh-key", i]
    if opts.username:
        node_cmd += ["--ssh-user", opts.username]
    if opts.canonization_transport:
        node_cmd += ["--transport", opts.canonization_transport]

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "inputs": [filename],
        # Don't cache this node - it has external temporary file as input
        "uid": uid_gen.get_random_uid("frepkage_upload"),
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": [],
        "cache": False,
        "target_properties": {},
        "outputs": [node_log_path, node_output],
        "kv": {
            "p": "UL",
            "pc": "light-cyan",
            "show_out": True,
        },
        "requirements": {
            "network": "full",
        },
        "cmds": [{"cmd_args": node_cmd, "cwd": "$(BUILD_ROOT)"}],
    }
    return node, node_output


def inject_canonization_result_node(tests, graph, canonization_nodes, opts):
    log_path = os.path.join("$(BUILD_ROOT)", "canonization_show_res.log")
    filter_descr = ", ".join(_get_skipped_tests_annotations(tests))
    injected_tests, stripped_tests = split_stripped_tests(tests, opts)
    all_resources = {}
    for suite in injected_tests:
        all_resources.update(suite.global_resources)

    cmd = util_tools.get_test_tool_cmd(opts, "canonization_result_node", all_resources) + ["--log-path", log_path]

    if opts and opts.tests_filters:
        for tf in opts.tests_filters:
            cmd.extend(["--test-name-filter", tf])

    if filter_descr:
        cmd += ["--filter-description", filter_descr]

    canonization_result_node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "broadcast": False,
        "cache": False,
        "inputs": PROJECTS_FILE_INPUTS,
        "env": sysenv.get_common_env().dump(),
        "uid": uid_gen.get_random_uid(),
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique([node["uid"] for node in canonization_nodes]),
        "target_properties": {},
        "outputs": [log_path],
        "kv": {
            "p": "CANONIZE",
            "pc": 'green',
            "show_out": True,
        },
        "requirements": gen_plan.get_requirements(opts, {"network": "restricted"}),
        "cmds": _generate_projects_file_cmds(injected_tests, opts=opts) + [{"cmd_args": cmd, "cwd": "$(BUILD_ROOT)"}],
    }
    graph.append_node(canonization_result_node, add_to_result=True)
    return canonization_result_node


def inject_tests_result_node(arc_root, graph, tests, test_opts, res_node_deps=None):
    res_node_deps = res_node_deps or []
    # all_tests contains skipped suites with required annotations
    filter_descr = ", ".join(_get_skipped_tests_annotations(tests))

    injected_tests, stripped_tests = split_stripped_tests(tests, test_opts)

    if test_opts.test_console_report and not test_opts.canonize_tests:
        not_skipped_suites = [s for s in injected_tests if not s.is_skipped()]
        if test_opts.allure_report and not_skipped_suites:
            allure_uid = inject_allure_report_node(
                graph,
                not_skipped_suites,
                allure_path=test_opts.allure_report,
                opts=test_opts,
                extra_deps=res_node_deps,
            )
            res_node_deps.append(allure_uid)
        if getattr(test_opts, "list_tests", False):
            listing_suites = []
            if test_opts.report_skipped_suites or test_opts.report_skipped_suites_only:
                listing_suites += stripped_tests
            if not test_opts.report_skipped_suites_only:
                listing_suites += injected_tests
            inject_list_result_node(graph, listing_suites, test_opts, filter_descr)


def inject_allure_report_node(graph, tests, allure_path, opts=None, extra_deps=None):
    allure_tars = []
    deps = uid_gen.get_test_result_uids(tests)
    if extra_deps:
        deps += extra_deps

    uid = "allure-report-{}".format(imprint.combine_imprints(*deps))

    all_resources = {}
    for suite in tests:
        all_resources.update(suite.global_resources)
        for res_uid in suite.result_uids:
            for o in get_outputs(graph, res_uid):
                if o.endswith("allure.tar"):
                    allure_tars.append(o)
    node_log_path = os.path.join("$(BUILD_ROOT)", "allure_report.log")
    allure_cmd = util_tools.get_test_tool_cmd(opts, "create_allure_report", all_resources) + [
        "--allure",
        allure_path,
        "--log-path",
        node_log_path,
    ]

    for allure_tar in allure_tars:
        allure_cmd += ["--allure-tars", allure_tar]

    allure_cmd += util_shared.get_oauth_token_options(opts)

    node = {
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "cache": False,
        "broadcast": False,
        "inputs": allure_tars,
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(deps),
        "env": sysenv.get_common_env().dump(),
        "target_properties": {},
        "outputs": [node_log_path],
        'kv': {
            "p": "AL",
            "pc": 'light-cyan',
            "show_out": True,
            "add_to_report": False,
        },
        "cmds": _generate_projects_file_cmds(tests, opts=opts) + [{"cmd_args": allure_cmd, "cwd": "$(BUILD_ROOT)"}],
    }
    graph.append_node(node, add_to_result=True)

    return uid


def _inject_canonize_node(graph, suite, sandbox_url, owner, keys, user, transport, opts):
    uid = uid_gen.get_uid(suite.output_uids, "canonize")
    test_out_path = suite.work_dir()
    node_log_path = os.path.join(test_out_path, "canonize.log")
    node_result_path = os.path.join(test_out_path, devtools.ya.test.const.CANONIZATION_RESULT_FILE_NAME)
    node_cmd = util_tools.get_test_tool_cmd(opts, "canonize", suite.global_resources) + [
        "--source-root",
        "$(SOURCE_ROOT)",
        "--build-root",
        "$(BUILD_ROOT)",
        "--sandbox",
        sandbox_url,
        "--output",
        test_out_path,
        "--resource-ttl",
        str(yalibrary.upload.consts.TTL_INF),
        "--max-file-size",
        str(canon_data.MAX_DEFAULT_FILE_SIZE),
        "--log-path",
        node_log_path,
        "--result-path",
        node_result_path,
        # "--log-level", "DEBUG",
    ]

    suite_node = graph.get_node_by_uid(suite.uid)
    if opts.custom_canondata_path:
        node_cmd += ["--custom-canondata-path", opts.custom_canondata_path]
    if opts.dir_outputs:
        node_cmd += ["--dir-outputs"]
        for output in suite_node["dir_outputs"]:
            if os.path.basename(output) == devtools.ya.test.const.TESTING_OUT_DIR_NAME:
                node_cmd += ["--input", os.path.dirname(output)]
    else:
        for output in suite_node["outputs"]:
            if os.path.basename(output) == devtools.ya.test.const.TESTING_OUT_TAR_NAME:
                node_cmd += ["--input", os.path.dirname(output)]

    if owner:
        node_cmd += ["--owner", owner]
    node_cmd += util_shared.get_oauth_token_options(opts)
    if opts.custom_fetcher and not opts.use_distbuild:
        node_cmd += ["--custom-fetcher", opts.custom_fetcher]
    if keys:
        for i in keys:
            node_cmd += ["--key", i]
    if user:
        node_cmd += ["--user", user]
    if transport:
        node_cmd += ["--transport", transport]
    if not opts.sandbox:
        node_cmd += ["--mds"]

    if suite.meta.canonize_sub_path:
        node_cmd += ["--sub-path", suite.meta.canonize_sub_path]

    if suite.save_old_canondata:
        node_cmd += ["--save-old-canondata"]

    if opts.no_src_changes:
        node_cmd += ["--no-src-changes"]

    if opts.canonization_backend:
        node_cmd += ["--backend", opts.canonization_backend]

    node = {
        "broadcast": False,
        "inputs": [],
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(suite.output_uids) + list(get_test_build_deps_or_throw(suite)),
        "env": sysenv.get_common_env().dump(),
        "cache": True,
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "node-type": devtools.ya.test.const.NodeType.TEST_AUX,
        "outputs": [node_log_path, node_result_path],
        'kv': {
            "p": "CANONIZE",
            "pc": 'green',
            "show_out": True,
        },
        "cmds": [{"cmd_args": node_cmd, "cwd": "$(BUILD_ROOT)"}],
    }
    graph.append_node(node)
    return node


def inject_test_checkout_node(graph, tests, arc_root, opts=None):
    checout_uids = []
    all_resources = {}
    for suite in tests:
        all_resources.update(suite.global_resources)

    def inject_checkout_node(
        paths, log_file_name, force_update=False, weak=False, arc_rel_root_path=None, destination=None
    ):
        log_path = os.path.join("$(BUILD_ROOT)", log_file_name)
        if not destination:
            destination = arc_root
        checkout_cmd = util_tools.get_test_tool_cmd(opts, "checkout", all_resources) + [
            "--arc-root",
            arc_root,
            "--log-path",
            log_path,
            # "--log-level", "DEBUG"
        ]

        if destination:
            checkout_cmd += ["--destination", destination]

        if arc_rel_root_path:
            checkout_cmd += ["--arc-rel-root-path", arc_rel_root_path]

        if weak:
            # skip error "URL doesn't exist"
            checkout_cmd += ["--skip-svn-error-code", "E170000"]

        for path in paths:
            checkout_cmd += ["--path", path]

        if force_update:
            checkout_cmd += ["--force-update"]

        uid = uid_gen.get_random_uid("checkout")

        node = {
            "broadcast": False,
            "inputs": [],
            "uid": uid,
            "cache": False,
            "cwd": "$(BUILD_ROOT)",
            "priority": 0,
            "deps": testdeps.unique(copy.copy(checout_uids)),
            "env": sysenv.get_common_env().dump(),
            "target_properties": {},
            "outputs": [log_path],
            'kv': {
                "p": "CO",
                "pc": 'yellow',
                "show_out": True,
                "add_to_report": False,
            },
            "cmds": [{"cmd_args": checkout_cmd, "cwd": "$(BUILD_ROOT)"}],
        }
        graph.append_node(node)
        checout_uids.append(uid)

    arcadia_dirs = []
    arcadia_weak_dirs = []
    arcadia = "arcadia/"

    for suite in tests:
        for p in suite.get_test_related_paths(arcadia, opts):
            # Fuzzy test node adds data dependency to the $arcadia/fuzzing/{ROOT_RELATIVE_PROJECT_PATH}
            # to obtain corpus.json file. However, it might no be presented in the repository
            # (there was no initial commit with fuzz data) and checkout node will fail.
            # That's why we try to checkout such weak deps and don't fail if they are not presented
            if p.startswith(devtools.ya.test.const.CORPUS_DATA_ROOT_DIR):
                arcadia_weak_dirs.append(p)
            elif p.startswith(arcadia):
                arcadia_dirs.append(exts.strings.left_strip(p, arcadia))
            else:
                arcadia_dirs.append(p)

    for x in [arcadia_dirs, arcadia_weak_dirs]:
        x[:] = sorted(set(x))

    if arcadia_dirs:
        inject_checkout_node(arcadia_dirs, "checkout_test_arcadia_paths.log")

    if arcadia_weak_dirs:
        inject_checkout_node(arcadia_weak_dirs, "checkout_weak_test_arcadia_paths.log", weak=True)

    return checout_uids


def strip_fake_outputs(data):
    # drop metainfo and fake files
    return [o for o in data if os.path.splitext(o)[1] not in devtools.ya.test.const.FAKE_OUTPUT_EXTS]


def get_outputs(graph, uid):
    node = graph.get_node_by_uid(uid)
    return strip_fake_outputs({o for o in node["outputs"]})


def wrap_cmd(cmd, cwd=None):
    return {
        "cmd_args": cmd,
        "cwd": cwd or "$(BUILD_ROOT)",
    }


def inject_test_list_node(arc_root, graph, suite, opts, custom_deps, platform_descriptor, random_uid=True, modulo=1):
    if random_uid:
        uid = uid_gen.get_random_uid("list-node")
        suite._result_uids = [uid]
        suite._output_uids = []
    else:
        uid = suite.uid
    env = sysenv.get_common_env()
    sysenv.update_test_initial_env_vars(env, suite, opts)

    log_path = os.path.join("$(BUILD_ROOT)", suite.project_path, devtools.ya.test.const.LIST_NODE_LOG_FILE)
    list_out_dir = suite.work_dir(devtools.ya.test.const.LIST_NODE_RESULT_FILE)
    test_list_file = suite.work_dir(devtools.ya.test.const.TEST_LIST_FILE)
    output = [log_path]
    list_cmd = util_tools.get_test_tool_cmd(
        opts, "list_tests", suite.global_resources, run_on_target_platform=False
    ) + [
        "--test-suite-class",
        type(suite).__name__,
        "--log-path",
        log_path,
        "--test-type",
        suite.get_type(),
        "--test-info-path",
        list_out_dir,
        "--modulo",
        str(modulo),
        "--partition-mode",
        suite.get_fork_partition_mode(),
        "--split-by-tests",
        str(suite.get_fork_mode() == "subtests"),
    ]

    if add_list_node(opts, suite):
        list_cmd += ["--test-list-path", test_list_file]
        output.append(test_list_file)
    else:
        output.append(list_out_dir)

    for flt in opts.tests_filters + suite.get_additional_filters():
        list_cmd += ["--tests-filters", flt]

    if suite.test_size:
        list_cmd += ["--test-size", suite.test_size]

    if suite.is_skipped():
        list_cmd += ["--is-skipped"]

    if suite.tags:
        for tag in suite.tags:
            list_cmd += ["--test-tags", tag]

    list_cmd.extend(_get_env_arg(opts, suite))

    tests_cases = suite.get_computed_test_names(opts)
    if tests_cases:
        list_cmd += cmdline.get_base_environment_relative_options(suite)
        # There is no need to satisfy deps for listing, because suite provide computable test names
        deps = []

        filters = getattr(opts, 'tests_filters', [])
        filter_func = test_filter.make_testname_filter(filters) if filters else lambda x: True
        for test_name in filter(filter_func, tests_cases):
            list_cmd += ["--test-name", test_name]
    elif suite.is_skipped():
        list_cmd += cmdline.get_base_environment_relative_options(suite)
        deps = []
    else:
        list_cmd += cmdline.get_environment_relative_options(suite, opts)
        deps = custom_deps or []
        try:
            deps += list(get_test_build_deps_or_throw(suite))
        except _DependencyException:
            logger.warning("Skipping test '%s (%s)' due to unresolved dependencies", suite.project_path, suite.name)
            return

        # Sanity check, we won't have sbr's in opensource by definition
        if app_config.in_house:
            for resource in testdeps.get_test_sandbox_resources(suite):
                list_cmd.extend(["--sandbox-resource", str(resource)])
                resource_node_uid = sandbox_node.inject_download_sandbox_resource_node(
                    graph, resource, opts, suite.global_resources
                )
                deps.append(resource_node_uid)

            for resource in testdeps.get_test_ext_sbr_resources(suite, arc_root):
                list_cmd.extend(["--sandbox-resource", str(resource)])
                resource_node_uid = sandbox_node.inject_download_sandbox_resource_node(
                    graph, resource, opts, suite.global_resources
                )
                deps.append(resource_node_uid)

    kv = {
        "p": "TLS",
        "pc": 'white',
        "show_out": False,
        "path": os.path.join(suite.project_path, suite.name),
        "special_runner": suite.special_runner,
    }
    kv.update({('needs_resource' + r): True for r in suite.get_resources(opts)})

    try:
        origin_cmd = list_cmd + suite.get_list_cmd("$(SOURCE_ROOT)", "$(BUILD_ROOT)", opts)
    except NotImplementedError:
        logger.warning("Test listing is not supported for suite type '%s'", type(suite).__name__)
        return
    if suite.special_runner == 'ios':
        cmds = make_ios_cmds(
            origin_cmd, suite.uid, get_simctl_path(platform_descriptor), get_profiles_path(platform_descriptor), opts
        )
    elif suite.special_runner.startswith('ios.simctl.'):
        origin_cmd += [
            '--ios-app',
            '--ios-simctl',
            get_simctl_path(platform_descriptor),
            '--ios-device-type',
            suite.get_ios_device_type(),
            '--ios-runtime',
            suite.get_ios_runtime(),
        ]
        if suite.special_runner[len('ios.simctl.') :] in ('x86_64', 'i386', 'arm64'):
            origin_cmd += ['--ios-profiles', get_profiles_path(platform_descriptor)]

        cmds = [wrap_cmd(origin_cmd)]
    elif suite.special_runner.startswith('android.'):
        android_sdk_root = suite.global_resources.get(
            devtools.ya.test.const.ANDROID_SDK_ROOT, '$({})'.format(devtools.ya.test.const.ANDROID_SDK_ROOT)
        )
        android_avd_root = suite.global_resources.get(
            devtools.ya.test.const.ANDROID_AVD_ROOT, '$({})'.format(devtools.ya.test.const.ANDROID_AVD_ROOT)
        )
        origin_cmd += [
            '--android-app',
            '--android-sdk',
            android_sdk_root,
            '--android-avd',
            android_avd_root,
            '--android-arch',
            suite.special_runner[len('android.') :],
            '--android-activity',
            suite.get_android_apk_activity(),
        ]
        cmds = [wrap_cmd(origin_cmd)]
    else:
        cmds = [wrap_cmd(origin_cmd)]

    inputs = suite.get_list_cmd_inputs(opts)

    if suite.has_prepare_test_cmds():
        extra_cmds, extra_inputs = suite.get_prepare_test_cmds()
        inputs += extra_inputs
        cmds = extra_cmds + cmds

    list_node = {
        "broadcast": False,
        "cache": False,
        "inputs": testdeps.unique(inputs),
        "uid": uid,
        "cwd": "$(BUILD_ROOT)",
        "priority": _get_suite_priority(suite),
        "deps": testdeps.unique(deps),
        "env": env.dump(),
        "target_properties": {
            "module_lang": suite.meta.module_lang,
        },
        "outputs": output,
        'kv': kv,
        "cmds": cmds,
    }
    return list_node


def inject_test_list_nodes(arc_root, graph, tests, opts, custom_deps, platform_descriptor):
    injected_tests = []
    for suite in tests:
        list_node = inject_test_list_node(arc_root, graph, suite, opts, custom_deps, platform_descriptor)
        if list_node:
            graph.append_node(list_node, add_to_result=False)
            injected_tests.append(suite)
    return injected_tests


def inject_list_result_node(graph, tests, opts, tests_filter_descr):
    if not tests:
        return
    list_node_uids = []

    all_resources = {}
    for suite in tests:
        list_node_uids.extend(suite.result_uids)
        all_resources.update(suite.global_resources)

    show_list_cmd = util_tools.get_test_tool_cmd(opts, "list_result_node", all_resources) + [
        "--fail-exit-code",
        str(devtools.ya.core.error.ExitCodes.TEST_FAILED),
    ]

    if tests_filter_descr:
        show_list_cmd += ["--filter-description", tests_filter_descr]

    if opts.report_skipped_suites or opts.report_skipped_suites_only:
        show_list_cmd += ["--report-skipped-suites"]

    for tf in getattr(opts, 'tests_filters', []) + getattr(opts, 'test_files_filter', []):
        show_list_cmd.extend(["--test-name-filter", tf])

    show_list_node = {
        "broadcast": False,
        "cache": False,
        "inputs": PROJECTS_FILE_INPUTS,
        "env": sysenv.get_common_env().dump(),
        "uid": uid_gen.get_random_uid("test-list-result"),
        "cwd": "$(BUILD_ROOT)",
        "priority": 0,
        "deps": testdeps.unique(list_node_uids),
        "target_properties": {},
        "outputs": [
            os.path.join("$(BUILD_ROOT)", devtools.ya.test.const.LIST_RESULT_NODE_LOG_FILE),
        ],
        'kv': {
            "p": "TL",
            "pc": 'white',
            "show_out": True,
        },
        "cmds": _generate_projects_file_cmds(tests, opts=opts) + [{"cmd_args": show_list_cmd, "cwd": "$(BUILD_ROOT)"}],
    }
    graph.append_node(show_list_node)

    return [show_list_node["uid"]]


def _generate_projects_file_cmds(tests, opts={}):
    cmds = []
    projects = []
    for suite in tests:
        work_dir = test_common.get_test_suite_work_dir(
            None,
            suite.project_path,
            suite.name,
            target_platform_descriptor=suite.target_platform_descriptor,
            multi_target_platform_run=suite.multi_target_platform_run,
        )
        projects.append(work_dir)

    for project in projects:
        cmds.append(
            {
                "cmd_args": test_common.get_python_cmd(opts=opts)
                + PROJECTS_FILE_INPUTS
                + [
                    "$(BUILD_ROOT)/projects.txt",
                    project,
                ]
            }
        )
    return cmds


def _get_skipped_tests_annotations(suites):
    by_message = {}
    for suite in suites:
        if suite.is_skipped():
            skipped_msg = suite.get_comment()
            if skipped_msg not in by_message:
                by_message[skipped_msg] = 0
            by_message[skipped_msg] += 1
    return ["{}: {}".format(msg, count) for msg, count in by_message.items()]


def split_stripped_tests(tests, opts):
    injected_tests, skipped_tests = [], []
    for t in tests:
        if t.is_skipped():
            skipped_tests.append(t)
        else:
            injected_tests.append(t)
    return injected_tests, skipped_tests
