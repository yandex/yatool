import os
import six
import logging
import multiprocessing

from humanfriendly import parse_size, parse_timespan, InvalidSize, InvalidTimespan

import app_config
import exts.path2
import exts.func

import yalibrary.upload.consts as upload_consts
from devtools.ya.core.yarg.groups import (
    OPERATIONAL_CONTROL_GROUP,
    CHECKOUT_ONLY_GROUP,
    OUTPUT_CONTROL_GROUP,
    RESULT_CONTROL_GROUP,
    PRINT_CONTROL_GROUP,
    PLATFORM_CONFIGURATION_GROUP,
    CACHE_CONTROL_GROUP,
    YT_CACHE_CONTROL_GROUP,
    YT_CACHE_PUT_CONTROL_GROUP,
    AUTOCHECK_GROUP,
    CODENAV_GROUP,
    FEATURES_GROUP,
    MAVEN_OPT_GROUP,
    GRAPH_GENERATION_GROUP,
)
from devtools.ya.core.yarg.help_level import HelpLevel
from devtools.ya.test import opts as test_opts
import devtools.ya.yalibrary.runner.schedule_strategy as schedule_strategy

from devtools.ya.core.common_opts import (
    PrintStatisticsOptions,
    BeVerboseOptions,
    DetailedArgsOptions,
    OutputStyleOptions,
    ShowHelpOptions,
    ProfileOptions,
    CustomSourceRootOptions,
    CustomBuildRootOptions,
    CustomMiscBuildInfoDirOptions,
    KeepTempsOptions,
    HtmlDisplayOptions,
    TeamcityOptions,
    ProfilerOptions,
    LogFileOptions,
    EventLogFileOptions,
    StdoutOptions,
    TerminalProfileOptions,
    MiniYaOpts,
    CrossCompilationOptions,
    CommonUploadOptions,
    BuildTypeConsumer,
    AuthOptions,
    DumpDebugCommonOptions,
    DumpDebugOptions,
    YaBin3Options,
)
from devtools.ya.core.yarg import (
    ArgConsumer,
    EnvConsumer,
    ConfigConsumer,
    SetValueHook,
    SetConstValueHook,
    BULLET_PROOF_OPT_GROUP,
    ADVANCED_OPT_GROUP,
    SetAppendHook,
    DEVELOPERS_OPT_GROUP,
    Options,
    FreeArgConsumer,
    ExtendHook,
    SetConstAppendHook,
    DictPutHook,
    DictUpdateHook,
    JAVA_BUILD_OPT_GROUP,
    ArgsValidatingException,
    AUTH_OPT_GROUP,
    SANDBOX_UPLOAD_OPT_GROUP,
    MDS_UPLOAD_OPT_GROUP,
    RawParamsOptions,
    return_true_if_enabled,
    NoValueDummyHook,
    SwallowValueDummyHook,
)

from library.python.fs import supports_clone
from devtools.ya.yalibrary.store.yt_store.opts_helper import parse_yt_max_cache_size

logger = logging.getLogger(__name__)

BUILD_THREADS_DEFAULT = 1


def parse_timespan_arg(span):
    try:
        return int(span)
    except ValueError:
        logger.debug('Failed to make int directly from span: %s. Will fallback to parse_timespan', span)

    return parse_timespan(span)


def parse_size_arg(size):
    try:
        return int(size)
    except ValueError:
        logger.debug('Failed to make int from size: %s. Will fallback to parse_size', size)

    return parse_size(size, binary=True)


# Reduce boilerplate for multiconsumer options
def make_opt_consumers(opt_name, help=None, arg_opts=None, env_opts=None, cfg_opts=None):
    result = []

    def evaluate(opts):
        result = {}
        for k, v in opts.items():
            if callable(v):
                result[k] = v(opt_name)
            else:
                result[k] = v
        return result

    if arg_opts is not None:
        arg_ev_opts = evaluate(arg_opts)
        if 'names' in arg_ev_opts:
            names = arg_ev_opts.pop['names']
        else:
            names = ['--' + opt_name.replace('_', '-')]
        result.append(ArgConsumer(names, help=help, **arg_ev_opts))

    if env_opts is not None:
        env_ev_opts = evaluate(env_opts)
        if 'name' in env_ev_opts:
            name = env_ev_opts.pop('name')
        else:
            name = 'YA_' + opt_name.upper()
        env_ev_opts.setdefault('hook', SetValueHook(opt_name))
        result.append(EnvConsumer(name, help=help, **env_ev_opts))

    if cfg_opts is not None:
        cfg_ev_opts = evaluate(cfg_opts)
        if 'name' in cfg_ev_opts:
            name = cfg_ev_opts.pop('name')
        else:
            name = opt_name
        result.append(ConfigConsumer(name, help=help, **cfg_ev_opts))

    return result


@exts.func.lazy
def get_cpu_count():
    return multiprocessing.cpu_count()


class BuildThreadsOptions(Options):
    def __init__(self, build_threads=BUILD_THREADS_DEFAULT):
        if build_threads is None:
            build_threads = get_cpu_count()

        self.build_threads = build_threads
        self.link_threads = 2

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-j', '--threads'],
                help='Build threads count',
                hook=SetValueHook('build_threads', int),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            EnvConsumer('YA_BUILD_THREADS', help='Build threads count', hook=SetValueHook('build_threads', int)),
            ConfigConsumer('build_threads'),
            ArgConsumer(
                ['--link-threads'],
                help='Link threads count',
                hook=SetValueHook('link_threads', int),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            EnvConsumer('YA_LINK_THREADS', help='Link threads count', hook=SetValueHook('link_threads', int)),
            ConfigConsumer('link_threads'),
        ]

    def postprocess2(self, params):
        if getattr(params, 'lto', False) or getattr(params, 'thinlto', False):
            self.link_threads = self.link_threads or 1


class DumpReportOptions(Options):
    def __init__(self, build_report_type='canonical'):
        self.build_results_report_file = None
        self.build_report_type = build_report_type
        self.build_results_resource_id = None
        self.build_results_report_tests_only = False
        self.report_skipped_suites = False
        self.report_skipped_suites_only = False
        self.use_links_in_report = False
        self.report_config_path = None
        self.dump_raw_results = False
        self.json_line_report_file = None
        self.dump_results2_json = False
        self.stat_only_report_file = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--build-results-report'],
                help='Dump build report to file in the --output-dir',
                hook=SetValueHook('build_results_report_file'),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--build-results-report-tests-only'],
                help='Report only test results in the report',
                hook=SetConstValueHook('build_results_report_tests_only', True),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--build-report-type'],
                help='Build report type(canonical, human_readable)',
                hook=SetValueHook('build_report_type'),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--build-results-resource-id'],
                help='Id of sandbox resource id containing build results',
                hook=SetValueHook('build_results_resource_id'),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--use-links-in-report'],
                help='Use links in report instead of local paths',
                hook=SetConstValueHook('use_links_in_report', True),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--report-skipped-suites'],
                help='Report skipped suites',
                hook=SetConstValueHook('report_skipped_suites', True),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--report-skipped-suites-only'],
                help='Report only skipped suites',
                hook=SetConstValueHook('report_skipped_suites_only', True),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--report-config'],
                help='Set path to TestEnvironment report config',
                hook=SetValueHook('report_config_path'),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--dump-raw-results'],
                help='Dump raw build results to the output root',
                hook=SetConstValueHook('dump_raw_results', True),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_SANDBOX_BUILD_OUTPUT_ID',
                help='Id of sandbox resource id containing build results',
                hook=SetValueHook('build_results_resource_id'),
            ),
            EnvConsumer(
                'YA_BUILD_RESULTS_REPORT',
                help='Dump build report to file in the --output-dir',
                hook=SetValueHook('build_results_report_file'),
            ),
            EnvConsumer(
                'YA_BUILD_RESULTS_TESTS_ONLY',
                help='Report only test results in the report',
                hook=SetConstValueHook('build_results_report_tests_only', True),
            ),
            EnvConsumer('YA_DUMP_RAW_RESULTS', hook=SetConstValueHook('dump_raw_results', True)),
            ArgConsumer(
                ['--jsonl-report'],
                help='Dump build results when they are ready to the specified file in jsonl format',
                hook=SetValueHook('json_line_report_file'),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_DUMP_RESULTS2_JSON',
                help='Dump build results to results2.json',
                hook=SetConstValueHook('dump_results2_json', True),
            ),
            ArgConsumer(
                ['--stat-only'],
                help='Predict build statistics without running the build. '
                'Cache based on content-only dynamic uids not considered. '
                'Output jsonl to file',
                hook=SetValueHook('stat_only_report_file'),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


class BuildTypeOptions(Options):
    def __init__(self, build_type='debug'):
        self.build_type = build_type
        self.sanitize = None
        self.sanitizer_flags = []
        self.lto = None
        self.thinlto = None
        self.musl = None
        self.sanitize_coverage = None
        self.use_afl = False
        self.hardening = False
        self.race = False
        self.cuda_platform = 'optional'

    @staticmethod
    def consumer():
        san_values = ['address', 'memory', 'thread', 'undefined', 'leak']
        cuda_values = ['optional', 'required', 'disabled']

        return [
            ArgConsumer(
                ['-d'],
                help='Debug build',
                hook=SetConstValueHook('build_type', 'debug'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['-r'],
                help='Release build',
                hook=SetConstValueHook('build_type', 'release'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            BuildTypeConsumer(['--build'], option='build_type', short_help='Build type', visible=HelpLevel.BASIC),
            ConfigConsumer('build_type'),
            ArgConsumer(
                ['--sanitize'],
                help='Sanitizer type',
                hook=SetValueHook('sanitize', values=san_values, values_limit=1),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--sanitizer-flag'],
                help='Additional flag for sanitizer',
                hook=SetAppendHook('sanitizer_flags'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--lto'],
                help='Build with LTO',
                hook=SetConstValueHook('lto', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--thinlto'],
                help='Build with ThinLTO',
                hook=SetConstValueHook('thinlto', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--sanitize-coverage'],
                help='Enable sanitize coverage',
                hook=SetValueHook('sanitize_coverage'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--afl'],
                help='Use AFL instead of libFuzzer',
                hook=SetConstValueHook('use_afl', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--musl'],
                help='Build with musl-libc',
                hook=SetConstValueHook('musl', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--hardening'],
                help='Build with hardening',
                hook=SetConstValueHook('hardening', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--race'],
                help='Build Go projects with race detector',
                hook=SetConstValueHook('race', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--cuda'],
                help='Cuda platform',
                hook=SetValueHook('cuda_platform', values=cuda_values),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
        ]


class YndexerOptions(Options):
    def __init__(self):
        self.yndexing = False
        self.yt_cluster = None
        self.yt_root = None
        self.yt_codenav_extra_opts = None
        self.java_yndexing = False
        self.kythe_to_proto_tool = None
        self.py3_yndexing = False
        self.py_yndexing = True
        self.ts_yndexing = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--yndexing'],
                help='Run yndexing',
                hook=SetConstValueHook('yndexing', True),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--yt-cluster'],
                help='Yt cluster',
                hook=SetValueHook('yt_cluster'),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--yt-root'],
                help='Yt upload root',
                hook=SetValueHook('yt_root'),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--java-yndexing'],
                help='Run java yndexing',
                hook=SetConstValueHook('java_yndexing', True),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--py3-yndexing'],
                help='Run python3 yndexing',
                hook=SetConstValueHook('py3_yndexing', True),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-py-yndexing'],
                help="Disable python2 yndexing",
                hook=SetConstValueHook('py_yndexing', False),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ts-yndexing'],
                help='Run TypeScript yndexing',
                hook=SetConstValueHook('ts_yndexing', True),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--kythe2proto-tool'],
                help='kythe entries to protos converter',
                hook=SetValueHook('kythe_to_proto_tool'),
                group=CODENAV_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_CODENAV_YT_UPLOADER_OPTS',
                help='Codenav yt upload command extra opts',
                hook=SetValueHook('yt_codenav_extra_opts'),
            ),
        ]

    def postprocess2(self, params):
        if self.java_yndexing:
            params.flags['JAVA_YNDEXING'] = 'yes'
        if self.ts_yndexing:
            params.flags['TS_YNDEXING'] = 'yes'


class RebuildOptions(Options):
    def __init__(self):
        self.clear_build = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--rebuild'],
                help='Rebuild all',
                hook=SetConstValueHook('clear_build', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
        ]


class PythonBuildOptions(Options):
    def __init__(self):
        self.external_py_files = False

    @staticmethod
    def consumer():
        return [
            # Configuration and env.var. consumer are missing intentionally:
            # this is a local development option.
            ArgConsumer(
                ['--ext-py'],
                help='Build binaries without embedded python files and load them from the filesystem at runtime.'
                'Only suitable for running tests to avoid linking binaries after every change in py-files.',
                hook=SetConstValueHook('external_py_files', True),
                group=ADVANCED_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
        ]

    def postprocess2(self, params):
        if self.external_py_files:
            if not getattr(params, 'run_tests', 0):
                logger.warning(
                    "You have requested external-py-files build mode without running tests. [[imp]]Don't use such binaries[[rst]]. Such binaries are only suitable for running tests"
                )
            if getattr(params, 'cache_tests', False):
                raise ArgsValidatingException('--ext-py cannot be used with --cache-tests')
            params.flags['EXTERNAL_PY_FILES'] = 'yes'


class DefaultNodeRequirementsOptions(Options):
    def __init__(self):
        self.default_node_requirements = {'network': 'restricted'}
        self.default_node_requirements_str = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--default-node-reqs'],
                help='Set default node requirements, use `None` to disable',
                hook=SetValueHook('default_node_requirements_str'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer('default_node_requirements', hook=SetValueHook('default_node_requirements_str')),
        ]

    def postprocess2(self, params):
        if self.default_node_requirements_str is not None:
            s = self.default_node_requirements_str
            if not s or s.lower() == 'none':
                # Let explicitly disable defaults
                self.default_node_requirements = None
                return
            try:
                import exts.yjson as json

                params.default_node_requirements = json.loads(s)
            except Exception:
                raise ArgsValidatingException('--default-node-reqs should have valid json format: {}'.format(s))

            if not isinstance(params.default_node_requirements, dict):
                raise ArgsValidatingException(
                    '--default-node-reqs should be valid json format dictionary: {}'.format(s)
                )


class StrictInputsOptions(Options):
    def __init__(self):
        self.strict_inputs = False
        self.sandboxing = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--strict-inputs'],
                help='Enable strict mode',
                hook=SetConstValueHook('strict_inputs', True),
                group=ADVANCED_OPT_GROUP,
                deprecated=True,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--sandboxing'],
                help='Run command in isolated source root',
                hook=SetConstValueHook('sandboxing', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
        ]

    def postprocess(self):
        if self.sandboxing:
            self.strict_inputs = True


class BuildTargetsOptions(Options):
    def __init__(self, with_free=False):
        self.build_targets = []
        self._free_build_targets = with_free

    def consumer(self):
        res = ArgConsumer(
            ['-C', '--target'],
            help='Targets to build',
            hook=SetAppendHook('build_targets'),
            group=OPERATIONAL_CONTROL_GROUP,
            visible=HelpLevel.BASIC,
        )
        if self._free_build_targets:
            res += FreeArgConsumer(help='target', hook=ExtendHook('build_targets'))

        return res

    def postprocess(self):
        self.build_targets = [y for x in self.build_targets for y in x.split(';')]


class ContinueOnFailOptions(Options):
    def __init__(self):
        self.continue_on_fail = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-k', '--keep-going'],
                help='Build as much as possible',
                hook=SetConstValueHook('continue_on_fail', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ConfigConsumer('continue_on_fail'),
        ]


class PGOOptions(Options):
    def __init__(self):
        self.pgo_add = False
        self.pgo_path = None
        self.pgo_user_path = []

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--pgo-add'],
                help='Create PGO profile. Profile is written to path specified in LLVM_PROFILE_FILE env var (default: '
                'default.profraw). Supports %p substitution for pid',
                hook=SetConstValueHook('pgo_add', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--pgo-use'],
                help='PGO profiles path. Supports glob',
                hook=SetAppendHook('pgo_user_path'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
        ]

    def postprocess(self):
        if self.pgo_add and self.pgo_user_path:
            raise ArgsValidatingException("Don't use --pgo-add and --pgo-use options together")

    def postprocess2(self, params):
        if params.pgo_add and getattr(params, 'clang_coverage', False):
            raise ArgsValidatingException("--pgo-add is not compatible with --clang-coverage")


class PICOptions(Options):
    def __init__(self):
        self.force_pic = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--pic'],
                help='Force PIC mode',
                hook=SetConstValueHook('force_pic', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            )
        ]


class SandboxAuthOptions(AuthOptions):
    visible = HelpLevel.ADVANCED

    """ Simple wrapper of AuthOptions for backward compatibility """

    def __init__(self, ssh_key_option_name="--key", ssh_user_option_name="--user", visible=None):
        super().__init__(ssh_key_option_name=ssh_key_option_name, visible=visible)
        self.ssh_user_option_name = ssh_user_option_name
        self.sandbox_oauth_token = None
        self.sandbox_oauth_token_path = None
        self.sandbox_oauth_token_path_depr = None
        self._sandbox_session_token = None

    def consumer(self):
        return super().consumer() + [
            ArgConsumer(
                ['--token'],
                help='oAuth token',
                hook=SetValueHook('sandbox_oauth_token', default_value=lambda _: '[HIDDEN]'),
                group=AUTH_OPT_GROUP,
            ),
            ConfigConsumer('sandbox_token', hook=SetValueHook('sandbox_oauth_token')),
            EnvConsumer('SANDBOX_TOKEN', help='oAuth token', hook=SetValueHook('sandbox_oauth_token')),
            EnvConsumer(
                'SB_TOKEN',
                help='Deprecated, use SANDBOX_TOKEN_PATH',
                hook=SetValueHook('sandbox_oauth_token_path_depr'),
            ),
            EnvConsumer('SANDBOX_TOKEN_PATH', help='oAuth token path', hook=SetValueHook('sandbox_oauth_token_path')),
            ArgConsumer(
                [self.ssh_user_option_name],
                help='Custom user name for authorization',
                hook=SetValueHook('username'),
                group=AUTH_OPT_GROUP,
            ),
            EnvConsumer(
                'SANDBOX_SESSION_TOKEN',
                help='oAuth token that is available only in SB task (takes precedance on everything else)',
                hook=SetValueHook('_sandbox_session_token'),
            ),
        ]

    def postprocess(self):
        super().postprocess()

        if self.sandbox_oauth_token:
            # Ignore token path
            self.sandbox_oauth_token_path = None
            self.sandbox_oauth_token_path_depr = None

        if self.sandbox_oauth_token_path_depr:
            logger.warning('SB_TOKEN env variable is deprecated, use SANDBOX_TOKEN_PATH instead')
            if not self.sandbox_oauth_token_path:
                self.sandbox_oauth_token_path = self.sandbox_oauth_token_path_depr
            self.sandbox_oauth_token_path_depr = None

        if self.sandbox_oauth_token_path:
            token = self._read_sandbox_token_file(self.sandbox_oauth_token_path)
            if token:
                self.sandbox_oauth_token = token
            else:
                self.sandbox_oauth_token_path = None

        if self._sandbox_session_token:
            # If the token is leaked we don't care, since it's available only inside a task
            logger.debug('Using SANDBOX_SESSION_TOKEN for authorization')
            self.sandbox_oauth_token = self._sandbox_session_token
            self.sandbox_oauth_token_path = None

        if not self.sandbox_oauth_token:
            # fall back to common oauth token if sandbox options are not set.
            # oauth_token and oauth_token_path are expected to be set in the superclass.
            # We should use sandbox_oauth_token instead of oauth_token for every interaction with Sandbox.
            self.sandbox_oauth_token = self.oauth_token
            self.sandbox_oauth_token_path = self.oauth_token_path

    @staticmethod
    def _read_sandbox_token_file(path):
        try:
            with open(path) as afile:
                token = afile.read().strip()
        except Exception as e:
            logger.debug('Could not read token file at %s: %s', path, e)
            return

        if not token:
            logger.debug('Attempted to read a token from %s, but the file is empty', path)
            return

        return token


class SandboxUploadOptions(SandboxAuthOptions):
    def __init__(
        self,
        ssh_key_option_name="--key",
        ssh_user_option_name="--user",
        sandbox_owner_option_name="--owner",
        visible=HelpLevel.ADVANCED,
    ):
        super().__init__(ssh_key_option_name, ssh_user_option_name, visible=visible)
        self.sandbox = False
        self.sandbox_url = upload_consts.DEFAULT_SANDBOX_URL
        self.resource_owner = None
        self.task_kill_timeout = None
        self.sandbox_owner_option_name = sandbox_owner_option_name

    def consumer(self):
        return super().consumer() + [
            ArgConsumer(
                [self.sandbox_owner_option_name],
                help='User name to own data saved to sandbox. Required in case of inf ttl of resources in mds.',
                hook=SetValueHook('resource_owner'),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--sandbox-url'],
                help='sandbox url to use for storing canonical file',
                hook=SetValueHook('sandbox_url'),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--task-kill-timeout'],
                help='Timeout in seconds for sandbox uploading task',
                hook=SetValueHook(name='task_kill_timeout', transform=int),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--sandbox'],
                help='Upload to Sandbox',
                hook=SetConstValueHook('sandbox', True),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
        ]

    def postprocess(self):
        super().postprocess()
        if not self.resource_owner:
            self.resource_owner = self.username
        if self.task_kill_timeout is not None:
            if self.task_kill_timeout <= 0:
                raise ArgsValidatingException("--task-kill-timeout must be a positive number")


class MavenImportOptions(SandboxUploadOptions):
    visible = HelpLevel.NONE

    def __init__(self, visible=None):
        super().__init__(sandbox_owner_option_name='--sandbox-owner', visible=visible)
        self.libs = []
        self.remote_repos = []
        self.contrib_owner = None
        self.dry_run = False
        self.unified_mode = True
        self.replace_version = {}
        self.skip_artifacts = []
        self.write_licenses = True
        self.canonize_licenses = True
        self.minimal_pom_validation = True
        self.local_jar_resources = not app_config.in_house
        self.import_dm = False
        self.ignore_errors = False
        self.repo_auth_username = None
        self.repo_auth_password = None

    def consumer(self):
        import devtools.ya.jbuild.maven.maven_import as mi

        return super().consumer() + [
            ArgConsumer(
                ['-o', '--owner'],
                help='Libraries owner. Default: {}.'.format(mi.DEFAULT_OWNER),
                hook=SetValueHook('contrib_owner'),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['-s'],
                help='User name to own data saved to sandbox',
                hook=SetValueHook('resource_owner'),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['-t'],
                help='oAuth token',
                hook=SetValueHook('sandbox_oauth_token', default_value=lambda _: '[HIDDEN]'),
                group=AUTH_OPT_GROUP,
            ),
            ArgConsumer(
                ['-r', '--remote-repository'],
                help='Specify remote repository manually to improve performance. Example: file://localhost/Users/me/repo',
                hook=SetAppendHook('remote_repos'),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['-d', '--dry-run'],
                help='Do not upload artifacts and do not modify contrib.',
                hook=SetConstValueHook('dry_run', True),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--legacy-mode'],
                help='Run legacy importer (instead of unified).',
                hook=SetConstValueHook('unified_mode', False),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--unified-mode'],
                help='Run unified importer (instead of legacy).',
                hook=SetConstValueHook('unified_mode', True),
                group=MAVEN_OPT_GROUP,
            ),
            FreeArgConsumer(help=mi.Artifact.FORMAT + ' ...', hook=SetValueHook('libs')),
            ArgConsumer(
                ['--replace-version'],
                help='Replace dependency lib version (or specify if missing)',
                hook=DictPutHook('replace_version'),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--skip'],
                help='Skip artifacts that cause error during import',
                hook=SetAppendHook('skip_artifacts'),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--no-write-licenses'],
                help='Write contribs licenses into ya.make\'s',
                hook=SetConstValueHook('write_licenses', False),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--no-canonize-licenses'],
                help='Canonize contrib licenses names via ya tool license_analyzer',
                hook=SetConstValueHook('canonize_licenses', False),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--minimal-pom-validation'],
                help='Use the minimum amount of pom file checks',
                hook=SetConstValueHook('minimal_pom_validation', True),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--strict-pom-validation'],
                help='Check pom for compliance with standard maven2',
                hook=SetConstValueHook('minimal_pom_validation', False),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--local-resources'],
                help='Local resources in repo instead upload to sandbox',
                hook=SetConstValueHook('local_jar_resources', True),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--import-managed-deps'],
                help='Import artifacts from Dependency Managements',
                hook=SetConstValueHook('import_dm', True),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--ignore-errors'],
                help='Skip dependency from dependency management on error',
                hook=SetConstValueHook('ignore_errors', True),
                group=MAVEN_OPT_GROUP,
            ),
            ArgConsumer(
                ['--repo-auth-username'],
                help='Username for repo with authentication (like bucket)',
                hook=SetValueHook('repo_auth_username'),
                group=AUTH_OPT_GROUP,
            ),
            ArgConsumer(
                ['--repo-auth-password'],
                help='Username for repo with authentication (like bucket)',
                hook=SetValueHook('repo_auth_password'),
                group=AUTH_OPT_GROUP,
            ),
        ]


class MDSUploadOptions(Options):
    def __init__(self, visible=HelpLevel.ADVANCED):
        super().__init__(visible=visible)
        self.mds = False
        self.mds_host = upload_consts.DEFAULT_MDS_HOST
        self.mds_port = upload_consts.DEFAULT_MDS_PORT
        self.mds_namespace = upload_consts.DEFAULT_MDS_NAMESPACE
        self.mds_token = None

    def consumer(self):
        return [
            ArgConsumer(
                ['--mds'],
                help='Upload to MDS',
                hook=SetConstValueHook('mds', True),
                group=MDS_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--mds-host'],
                help='MDS Host',
                hook=SetValueHook(name='mds_host'),
                group=MDS_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--mds-port'],
                help='MDS Port',
                hook=SetValueHook(name='mds_port'),
                group=MDS_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--mds-namespace'],
                help='MDS namespace',
                hook=SetValueHook(name='mds_namespace'),
                group=MDS_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--mds-token'],
                help='MDS Basic Auth token',
                hook=SetValueHook(name='mds_token', default_value=lambda _: '[HIDDEN]'),
                group=MDS_UPLOAD_OPT_GROUP,
            ),
            ConfigConsumer('mds'),
        ]


class FindPathOptions(Options):
    def __init__(self):
        self.max_dist = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--max-dist'],
                help='Max path length',
                hook=SetValueHook('max_dist', transform=int),
                group=BULLET_PROOF_OPT_GROUP,
            ),
        ]


class ExecutorOptions(Options):
    def __init__(self):
        self.local_executor = True
        self.executor_address = None
        self.eager_execution = False
        self.use_clonefile = True
        self.runner_dir_outputs = True
        self.dir_outputs_test_mode = False
        self.schedule_strategy = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--local-executor'],
                help='Use local executor instead of Popen',
                hook=SetConstValueHook('local_executor', True),
                group=FEATURES_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-local-executor'],
                help='Use Popen instead of local executor',
                hook=SetConstValueHook('local_executor', False),
                group=FEATURES_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer('YA_LOCAL_EXECUTOR', hook=SetValueHook('local_executor', return_true_if_enabled)),
            ArgConsumer(
                ['--eager-execution'],
                help='Run tasks on the fly as soon as possible (mainly used in cache heaters)',
                hook=SetConstValueHook('eager_execution', True),
                group=FEATURES_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer('YA_EAGER_EXECUTION', hook=SetValueHook('eager_execution', return_true_if_enabled)),
            ConfigConsumer('local_executor'),
            ConfigConsumer('eager_execution'),
            ArgConsumer(
                ['--executor-address'],
                help="Don't start local executor, but use provided",
                hook=SetValueHook('executor_address'),
                group=ADVANCED_OPT_GROUP,
                visible=False,
            ),
            # TODO remove --dir-outputs-test-mode option after https://st.yandex-team.ru/DEVTOOLS-9769
            ArgConsumer(
                ['--dir-outputs-test-mode'],
                help='Enable new dir outputs features',
                hook=SetConstValueHook('dir_outputs_test_mode', True),
                group=FEATURES_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer('dir_outputs_test_mode'),
            EnvConsumer('YA_DIR_OUTPUTS_TEST_MODE', hook=SetValueHook('dir_outputs_test_mode', return_true_if_enabled)),
            ArgConsumer(
                ['--disable-runner-dir-outputs'],
                help='Disable dir_outputs support in runner',
                hook=SetConstValueHook('runner_dir_outputs', False),
                group=FEATURES_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer('runner_dir_outputs'),
            EnvConsumer('YA_RUNNER_DIR_OUTPUTS', hook=SetValueHook('runner_dir_outputs', return_true_if_enabled)),
            ArgConsumer(
                ['--use-clonefile'],
                help='Use clonefile instead of hardlink on macOS',
                hook=SetConstValueHook('use_clonefile', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.NONE,
            ),
            EnvConsumer('YA_USE_CLONEFILE', hook=SetValueHook('use_clonefile', return_true_if_enabled)),
            ConfigConsumer('use_clonefile'),
            ArgConsumer(
                ['--no-clonefile'],
                help='Disable clonefile option',
                hook=SetConstValueHook('use_clonefile', False),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            EnvConsumer('YA_NO_CLONEFILE', hook=SetValueHook('use_clonefile', False)),
            ConfigConsumer('schedule_strategy'),
            ArgConsumer(
                ['--schedule-strategy'],
                help=(
                    "Order in which the runner picks up tasks ready for execution. "
                    "To find out more about strategies see yalibrary.runner library. "
                ),
                hook=SetValueHook('schedule_strategy', values=schedule_strategy.strategy_names()),
                group=ADVANCED_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
        ]

    def postprocess2(self, params):
        if (
            self.eager_execution
            and getattr(params, 'run_tests', 0) > 0
            and not getattr(params, 'continue_on_fail', True)
        ):
            raise ArgsValidatingException("eager_execution with testing should be used with -k, see DEVTOOLS-7068")
        if self.use_clonefile and supports_clone():
            import yalibrary.runner.fs as yrfs

            yrfs.enable_clonefile()


class GraphFilterOutputResultOptions(Options):
    def __init__(self):
        self.add_result = []
        self.add_host_result = []
        self.replace_result = False
        self.all_outputs_to_result = False
        self.add_binaries_to_results = False

    @staticmethod
    def consumer():
        protobuf_exts = ['.pb.h', '.pb.cc', '.pb.go', '_pb2.py', '_pb2_grpc.py', '_pb2.pyi']
        flatbuf_exts = ['.fbs.h', '.fbs.gosrc', '.fbs.jsrc', '.fbs.pysrc']

        return [
            ArgConsumer(
                ['--add-result'],
                help='Process selected build output as a result',
                hook=SetAppendHook('add_result'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--add-host-result'],
                help='Process selected host build output as a result',
                hook=SetAppendHook('add_host_result'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--add-protobuf-result'],
                help='Process protobuf output as a result',
                hook=SetConstAppendHook('add_result', protobuf_exts),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--add-flatbuf-result'],
                help='Process flatbuf output as a result',
                hook=SetConstAppendHook('add_result', flatbuf_exts),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--all-outputs-to-result'],
                help='Process all outputs of the node along with selected build output as a result',
                hook=SetConstValueHook('all_outputs_to_result', True),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer('add_result'),
            ConfigConsumer('add_result_extend', hook=ExtendHook('add_result')),
            ConfigConsumer('add_host_result'),
            ConfigConsumer('all_outputs_to_result'),
            ArgConsumer(
                ['--replace-result'],
                help='Build only --add-result targets',
                hook=SetConstValueHook('replace_result', True),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--add-binaries-to-results'],
                help='Add all binary targets (bin/so) to results',
                hook=SetConstValueHook('add_binaries_to_results', True),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
        ]


class GraphOperateResultsOptions(Options):
    def __init__(self):
        self.add_modules_to_results = False
        self.gen_renamed_results = True
        self.strip_packages_from_results = False
        self.strip_binary_from_results = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--add-modules-to-results'],
                help='Process all modules as results',
                hook=SetConstValueHook('add_modules_to_results', True),
                group=RESULT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer(
                'YA_NO_GEN_RENAMED_RESULTS',
                help='Generate renamed results in cross-builds',
                hook=SetConstValueHook('gen_renamed_results', False),
            ),
            ArgConsumer(
                ['--strip-packages-from-results'],
                help='Strip all packages from results',
                hook=SetConstValueHook('strip_packages_from_results', True),
                group=RESULT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer(
                'YA_STRIP_PACKAGES_FROM_RESULTS',
                help='Strip all packages from results',
                hook=SetConstValueHook('strip_packages_from_results', True),
            ),
            ArgConsumer(
                ['--strip-binary-from-result'],
                help="Strip all binaries from results",
                hook=SetConstValueHook('strip_binary_from_results', True),
                group=RESULT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
        ]


class YaMakeOptions(Options):
    def __init__(self):
        self.do_clear = False
        self.show_command = []
        self.show_timings = False
        self.ext_progress = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--clear'],
                help='Clear temporary data',
                hook=SetConstValueHook('do_clear', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--show-command'],
                help='Print command for selected build output',
                hook=SetAppendHook('show_command'),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ConfigConsumer('show_timings'),
            ConfigConsumer('show_extra_progress', hook=SetValueHook('ext_progress')),
            ArgConsumer(
                ['--show-timings'],
                help='Print execution time for commands',
                hook=SetConstValueHook('show_timings', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--show-extra-progress'],
                help='Print extra progress info',
                hook=SetConstValueHook('ext_progress', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
        ]


class ContentUidsOptions(Options):
    def __init__(self):
        self.content_uids = True
        self.validate_build_root_content = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--content-uids'],
                help='Enable additional cache based on content-only dynamic uids [default]',
                hook=SetConstValueHook('content_uids', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=False,
            ),
            ArgConsumer(
                ['--no-content-uids'],
                help='Disable additional cache based on content-only dynamic uids',
                hook=SetConstValueHook('content_uids', False),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--force-content-uids'],
                help='Deprecated, do nothing',
                hook=NoValueDummyHook(),
                group=DEVELOPERS_OPT_GROUP,
                visible=False,
            ),
            ConfigConsumer('content_uids'),
            EnvConsumer(
                'YA_USE_CONTENT_UIDS',
                help='Enable additional cache based on content-only dynamic uids (depends on cache version)',
                hook=SetValueHook('content_uids', return_true_if_enabled),
            ),
            ArgConsumer(
                ['--validate-build-root-content'],
                help='Validate build root content by cached hash in content_uids mode',
                hook=SetConstValueHook('validate_build_root_content', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=False,
            ),
            ConfigConsumer('validate_build_root_content'),
            EnvConsumer('YA_VALIDATE_BUILD_ROOT_CONTENT', hook=SetValueHook('validate_build_root_content')),
        ]

    def postprocess(self):
        if not self.content_uids:
            self.validate_build_root_content = False


class ForceDependsOptions(Options):
    def __init__(self):
        self.force_build_depends = False

    @staticmethod
    def consumer():
        return [
            ConfigConsumer('force_build_depends'),
            ArgConsumer(
                ['--force-build-depends'],
                help='Build targets reachable by RECURSE_FOR_TESTS and DEPENDS macros',
                hook=SetConstValueHook('force_build_depends', True),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            EnvConsumer(
                'YA_FORCE_BUILD_DEPENDS',
                hook=SetValueHook('force_build_depends', transform=return_true_if_enabled),
            ),
        ]


class IgnoreRecursesOptions(Options):
    def __init__(self):
        self.ignore_recurses = False

    @staticmethod
    def consumer():
        return [
            ConfigConsumer('ignore_recurses'),
            ArgConsumer(
                ['--ignore-recurses', '-R'],
                help='Do not build by RECURSES',
                hook=SetConstValueHook('ignore_recurses', True),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            EnvConsumer(
                'YA_IGNORE_RECURSES', help='Do not build by RECURSES', hook=SetConstValueHook('ignore_recurses', True)
            ),
        ]


class CreateSymlinksOptions(Options):
    def __init__(self):
        self.create_symlinks = True
        self.symlink_root = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--no-src-links'],
                help='Do not create any symlink in source directory',
                hook=SetConstValueHook('create_symlinks', False),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--result-store-root'],
                help='Result store root',
                hook=SetValueHook('symlink_root'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer('create_symlinks'),
            ConfigConsumer('symlink_root'),
        ]


class SetNiceValueOptions(Options):
    def __init__(self):
        self.set_nice_value = 10

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--nice'],
                help='Set nice value for build processes',
                hook=SetValueHook('set_nice_value', transform=int),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ConfigConsumer('set_nice_value'),
        ]


class ShowSavedWarningsOptions(Options):
    def __init__(self):
        self.show_saved_warnings = False

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['-w'],
            help='Show saved warnings',
            hook=SetConstValueHook('show_saved_warnings', True),
            group=BULLET_PROOF_OPT_GROUP,
        )


# Legacy
class InstallDirOptions(Options):
    def __init__(self):
        self.install_dir = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-I', '--install'],
                help='Path to accumulate resulting binaries and libraries',
                hook=SetValueHook('install_dir'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ConfigConsumer('install_dir'),
        ]


# Legacy support for implicit install dir within results root for Sandbox
# TODO: wipe
class GenerateLegacyDirOptions(Options):
    def __init__(self):
        self.generate_bin_dir = None
        self.generate_lib_dir = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--generate-bin-dir'],
                help='Generate bin directory in results',
                hook=SetValueHook('generate_bin_dir'),
                group=OUTPUT_CONTROL_GROUP,
                visible=False,
                deprecated=True,
            ),
            EnvConsumer(
                'YA_GENERATE_BIN_DIR', help='Generate bin directory in results', hook=SetValueHook('generate_bin_dir')
            ),
            ArgConsumer(
                ['--generate-lib-dir'],
                help='Generate lib directory in results',
                hook=SetValueHook('generate_lib_dir'),
                group=OUTPUT_CONTROL_GROUP,
                visible=False,
                deprecated=True,
            ),
            EnvConsumer(
                'YA_GENERATE_LIB_DIR', help='Generate lib directory in results', hook=SetValueHook('generate_lib_dir')
            ),
        ]


class DumpMetaOptions(Options):
    def __init__(self):
        self.dump_meta = None

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--dump-meta'], help='Dump meta to file', hook=SetValueHook('dump_meta'), group=DEVELOPERS_OPT_GROUP
        )


class ArcPrefetchOptions(Options):
    def __init__(self, prefetch=False, visible=None):
        super().__init__(visible=visible)
        self.prefetch = prefetch

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--prefetch'],
                help='Prefetch directories needed for build',
                hook=SetConstValueHook('prefetch', True),
                group=CHECKOUT_ONLY_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--no-prefetch'],
                help='Do not prefetch directories needed for build',
                hook=SetConstValueHook('prefetch', False),
                group=CHECKOUT_ONLY_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            EnvConsumer('YA_PREFETCH', hook=SetValueHook('prefetch', return_true_if_enabled)),
            ConfigConsumer('prefetch'),
        ]


class YMakeModeOptions(Options):
    def __init__(self):
        self.ymake_tool_servermode = False
        self.ymake_pic_servermode = False
        self.ymake_multiconfig = False
        self.force_ymake_multiconfig = False
        self.ymake_parallel_rendering = False
        self.ymake_internal_servermode = False
        self.ymake_use_subinterpreters = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--ymake-tool-servermode'],
                help='Pass targets to tool ymake via evlog',
                hook=SetConstValueHook('ymake_tool_servermode', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-ymake-tool-servermode'],
                help='Pass targets to tool ymake via command line',
                hook=SetConstValueHook('ymake_tool_servermode', False),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_YMAKE_TOOL_SERVERMODE',
                hook=SetValueHook('ymake_tool_servermode', return_true_if_enabled),
            ),
            ConfigConsumer('ymake_tool_servermode'),
            ArgConsumer(
                ['--ymake-pic-servermode'],
                help='Pass targets to pic ymake via evlog',
                hook=SetConstValueHook('ymake_pic_servermode', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-ymake-pic-servermode'],
                help='Pass targets to pic ymake via command line',
                hook=SetConstValueHook('ymake_pic_servermode', False),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_YMAKE_PIC_SERVERMODE',
                hook=SetValueHook('ymake_pic_servermode', return_true_if_enabled),
            ),
            ConfigConsumer('ymake_pic_servermode'),
            ArgConsumer(
                ['--ymake-multiconfig'],
                help='Run one ymake for all configurations',
                hook=SetConstValueHook('ymake_multiconfig', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-ymake-multiconfig'],
                help='Run one ymake for each configuration',
                hook=SetConstValueHook('ymake_multiconfig', False),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--force-ymake-multiconfig'],
                help='Run one ymake for all configurations regardless of the number of target platforms',
                hook=SetConstValueHook('force_ymake_multiconfig', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ymake-use-subinterpreters'],
                help='Use Python subinterpreters',
                hook=SetConstValueHook('ymake_use_subinterpreters', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-ymake-use-subinterpreters'],
                help='Do not use Python subinterpreters',
                hook=SetConstValueHook('ymake_use_subinterpreters', False),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_YMAKE_MULTICONFIG',
                hook=SetValueHook('ymake_multiconfig', return_true_if_enabled),
            ),
            EnvConsumer(
                'YA_FORCE_YMAKE_MULTICONFIG',
                hook=SetValueHook('force_ymake_multiconfig', return_true_if_enabled),
            ),
            EnvConsumer(
                'YA_YMAKE_USE_SUBINTERPRETERS',
                hook=SetValueHook('ymake_use_subinterpreters', return_true_if_enabled),
            ),
            EnvConsumer(
                'YA_YMAKE_PARALLEL_RENDERING',
                hook=SetValueHook('ymake_parallel_rendering', return_true_if_enabled),
            ),
            ConfigConsumer('ymake_multiconfig'),
            ConfigConsumer('force_ymake_multiconfig'),
            ConfigConsumer('ymake_parallel_rendering'),
            ConfigConsumer('ymake_internal_servermode'),
            ConfigConsumer('ymake_use_subinterpreters'),
        ]

    def postprocess2(self, params):
        if self.force_ymake_multiconfig:
            self.ymake_multiconfig = True
            logger.debug('Ymake multiconfig is forced for more than one target platform')
        elif self.ymake_multiconfig and len(getattr(params, 'target_platforms', [])) > 1:
            self.ymake_multiconfig = False
            logger.debug('Ymake multiconfig is disabled for more than one target platform')
        if not self.ymake_multiconfig:
            self.ymake_internal_servermode = False
            logger.debug('Ymake internal servermode is available only in multiconfig mode')
            self.ymake_parallel_rendering = False
            logger.debug('Ymake parallel rendering is available only in multiconfig mode')
            self.ymake_use_subinterpreters = False
            logger.debug('Ymake subinterpreters is available only in multiconfig mode')


class YMakeBinOptions(Options):
    def __init__(self):
        self.ymake_bin = None
        self.no_ymake_resource = False
        self.no_yabin_resource = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--ymake-bin'],
                help='Path to ymake binary',
                hook=SetValueHook('ymake_bin'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-ymake-resource'],
                help='Do not use ymake binary as part of build commands',
                hook=SetConstValueHook('no_ymake_resource', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--no-ya-bin-resource'],
                help='Do not use ya-bin binary as part of build commands',
                hook=SetConstValueHook('no_yabin_resource', True),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


def have_debug_option(params, key):
    return key in ''.join(getattr(params, 'debug_options', []))


class YMakeDebugOptions(Options):
    def __init__(self):
        self.debug_options = []
        self.clear_ymake_cache = False
        self.vcs_file = None
        self.dump_file_path = None
        self.trace_context_json = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--dev', '-x'],
                help='ymake debug options',
                hook=SetAppendHook('debug_options'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer('clear_ymake_cache', help='Clear ymake cache (-xx)', group=DEVELOPERS_OPT_GROUP),
            ArgConsumer(
                ['--vcs-file'],
                help='Provides VCS file',
                hook=SetValueHook('vcs_file'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--dump-files-path'],
                help='Put extra ymake dumps into specified directory',
                hook=SetValueHook('dump_file_path'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YANDEX_TRACE_CONTEXT_JSON',
                help='W3C Trace Context in JSON format',
                hook=SetValueHook('trace_context_json'),
            ),
        ]

    def postprocess(self):
        if self.clear_ymake_cache:
            self.debug_options.append('x')


class ConfigureDebugOptions(Options):
    def __init__(self):
        self.conf_debug_options = []
        self.extra_conf = None
        # TODO remove when YA-1456 is done
        self.ignore_configure_errors = True

    def consumer(self):
        return [
            ArgConsumer(
                ['--dev-conf'],
                help='Configure step debug options: list-files, print-commands, force-run, verbose-run',
                hook=SetAppendHook('conf_debug_options'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--extra-conf'],
                help='Use extra macro or module definitions from file',
                hook=SetValueHook('extra_conf'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ignore-configure-errors'],
                hook=SetValueHook('ignore_configure_errors'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_IGNORE_CONFIGURE_ERRORS',
                hook=SetValueHook('ignore_configure_errors', return_true_if_enabled),
            ),
            ConfigConsumer('ignore_configure_errors'),
        ]


class IgnoreNodesExitCode(Options):
    def __init__(self):
        self.ignore_nodes_exit_code = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--ignore-nodes-exit-code'],
                help='Ignore nodes exit code',
                hook=SetConstValueHook('ignore_nodes_exit_code', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


class YMakeDumpGraphOptions(Options):
    def __init__(self):
        self.dump_graph = None
        self.dump_graph_file = None
        self.use_json_cache = True
        self.save_context_to = None
        self.save_graph_to = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-G', '--dump-graph'],
                help='Dump full build graph to stdout',
                hook=SetConstValueHook('dump_graph', 'text'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--dump-json-graph'],
                help='Dump full build graph as json to stdout',
                hook=SetConstValueHook('dump_graph', 'json'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--dump-graph-to-file'],
                help='Dump full build graph to file instead stdout',
                hook=SetValueHook('dump_graph_file'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer(
                'use_json_cache', help='Use cache for json-graph in ymake (-xs)', group=DEVELOPERS_OPT_GROUP
            ),
            ArgConsumer(
                ['--save-context-to'],
                help='Save context and exit',
                hook=SetValueHook('save_context_to'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--save-graph-to'],
                help='File to save graph from context',
                hook=SetValueHook('save_graph_to'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


class YWarnModeOptions(Options):
    def __init__(self, warn_mode=None):
        self.warn_mode = warn_mode or ['dirloops', 'ChkPeers']

    def consumer(self):
        return ArgConsumer(
            ['--warning-mode'],
            help='Warning mode',
            hook=SetAppendHook('warn_mode'),
            group=OPERATIONAL_CONTROL_GROUP,
            visible=HelpLevel.ADVANCED,
        )

    def postprocess(self):
        self.warn_mode = [y for x in self.warn_mode for y in x.split(';')]


class SonarOptions(Options):
    def __init__(self):
        self.sonar = False
        self.sonar_project_filters = []
        self.sonar_properties = {}
        self.sonar_do_not_compile = False
        self.sonar_default_project_filter = False
        self.sonar_java_args = []

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--sonar'],
                help='Analyze code with sonar.',
                hook=SetConstValueHook('sonar', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--sonar-project-filter'],
                help='Analyze only projects that match any filter',
                hook=SetAppendHook('sonar_project_filters'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--sonar-default-project-filter'],
                help='Set default --sonar-project-filter value( build targets )',
                hook=SetConstValueHook('sonar_default_project_filter', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--sonar-property', '-N'],
                help='Property for sonar analyzer(name[=val], "yes" if val is omitted")',
                hook=DictPutHook('sonar_properties', default_value='yes'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--sonar-do-not-compile'],
                help='Do not compile java sources.'
                'In this case "-Dsonar.java.binaries" property is not set up automatically.',
                hook=SetConstValueHook('sonar_do_not_compile', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--sonar-java-args'],
                help='Java machine properties for sonar scanner run',
                hook=SetAppendHook('sonar_java_args'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
        ]


class UniversalFetcherOptions(Options):
    def __init__(self):
        self.use_universal_fetcher_everywhere = False
        self.use_universal_fetcher_for_dist_results = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--use-universal-fetcher-everywhere'],
                help='Universal fetcher impl',
                hook=SetConstValueHook('use_universal_fetcher_everywhere', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_USE_UNIVERSAL_FETCHER_EVERYWHERE',
                hook=SetValueHook('use_universal_fetcher_everywhere', return_true_if_enabled),
            ),
            ConfigConsumer('use_universal_fetcher_everywhere'),
            EnvConsumer(
                'YA_USE_UNIVERSAL_FETCHER_FOR_DIST_RESULTS',
                hook=SetValueHook('use_universal_fetcher_for_dist_results', return_true_if_enabled),
            ),
            ConfigConsumer('use_universal_fetcher_for_dist_results'),
        ]


class CustomFetcherOptions(Options):
    DEFAULT_PARAMS = [{'name': 'custom'}, {'name': 'proxy'}, {'name': 'skynet'}, {'name': 'mds'}, {'name': 'sandbox'}]

    def __init__(self):
        self.custom_fetcher = None
        self.fetcher_params = CustomFetcherOptions.DEFAULT_PARAMS
        self.fetcher_params_str = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--custom-fetcher'],
                help='Custom fetcher script for getting sandbox resources',
                hook=SetValueHook('custom_fetcher'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--fetcher-params'],
                help='Fetchers priorities and params',
                hook=SetValueHook('fetcher_params_str'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer(
                'YA_CUSTOM_FETCHER',
                hook=SetValueHook('custom_fetcher'),
            ),
            ConfigConsumer('fetcher_params', hook=SetValueHook('fetcher_params_str')),
        ]

    def _read_token_files(self):
        for fet in self.fetcher_params:
            if 'name' not in fet:
                self.fetcher_params = CustomFetcherOptions.DEFAULT_PARAMS
                return

            if 'token_file' not in fet or not os.path.isfile(fet['token_file']):
                continue

            token_file = fet['token_file']
            try:
                with open(token_file) as f:
                    token = f.read().strip()
                    if token:
                        fet['token'] = f.read().strip()
            except Exception:
                self.fetcher_params = CustomFetcherOptions.DEFAULT_PARAMS
                return

    def postprocess(self):
        self.fetcher_params = CustomFetcherOptions.DEFAULT_PARAMS

        s = self.fetcher_params_str
        try:
            import exts.yjson as json

            self.fetcher_params = json.loads(s)
        except Exception:
            return

        if not isinstance(self.fetcher_params, list):
            self.fetcher_params = CustomFetcherOptions.DEFAULT_PARAMS
            return

        self._read_token_files()


class FlagsOptions(Options):
    def __init__(self):
        self.flags = {}
        self.host_flags = {}

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-D'],
                help='Set variables (name[=val], "yes" if val is omitted)',
                hook=DictPutHook('flags', default_value='yes'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ConfigConsumer(
                'flags',
                hook=DictUpdateHook('flags'),
            ),
        ]


class OutputOptions(Options):
    def __init__(self):
        self.output_root = None
        self.suppress_outputs = []
        self.default_suppress_outputs = [
            '.o',
            '.obj',
            '.mf',
            '..',
            '.cpf',
            '.cpsf',
            '.srclst',
            '.fake',
            '.vet.out',
            '.vet.txt',
            '.self.protodesc',
        ]

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-o', '--output'],
                help='Directory with build results',
                hook=SetValueHook('output_root'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ConfigConsumer('output_root'),
            ArgConsumer(
                ['--results-root'],
                help='Alias for --output',
                hook=SetValueHook('output_root'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),  # XXX: Legacy
            EnvConsumer('YA_OUTPUT_ROOT', help='Set directory with build results', hook=SetValueHook('output_root')),
            ArgConsumer(
                ['--no-output-for'],
                help='Do not symlink/copy output for files with given suffix, they may still be save in cache as result',
                hook=SetAppendHook('suppress_outputs'),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer('suppress_outputs', hook=ExtendHook('suppress_outputs')),
            ArgConsumer(
                ['--no-output-default-for'],
                help='Do not symlink/copy output for files with given suffix if not overriden with --add-result',
                hook=SetAppendHook('default_suppress_outputs'),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer('default_suppress_outputs', hook=ExtendHook('default_suppress_outputs')),
        ]

    def postprocess(self):
        if self.output_root is not None:
            self.output_root = exts.path2.abspath(self.output_root, expand_user=True)


class CustomGraphAndContextOptions(Options):
    def __init__(self):
        self.custom_json = None
        self.custom_context = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--build-custom-json'],
                help='Build custom graph specified by file name',
                hook=SetValueHook('custom_json'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--custom-context'],
                help='Use custom context specified by file name (requires additionally passing --build-custom-json)',
                hook=SetValueHook('custom_context'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


class TestenvReportDirOptions(Options):
    def __init__(self):
        self.testenv_report_dir = None

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--testenv-report-dir'],
            help='Set directory for TestEnvironment report',
            hook=SetValueHook('testenv_report_dir'),
            group=AUTOCHECK_GROUP,
            visible=HelpLevel.INTERNAL,
            deprecated=True,
        )


class StreamReportOptions(Options):
    def __init__(self):
        self.streaming_stage_namespace = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--streaming-stage-namespace'],
                help='Infix to trace build stages with (will be prefixed with "distbuild/"',
                hook=SetValueHook('streaming_stage_namespace'),
                group=AUTOCHECK_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer('YA_STREAMING_STAGE_NAMESPACE', hook=SetValueHook('streaming_stage_namespace')),
        ] + StreamReportOptions._deprecated_options()

    # TODO Remove when we make sure no one uses it
    @staticmethod
    def _deprecated_options():
        return [
            ArgConsumer(
                ['--streaming-report-url'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--streaming-report-id'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--stream-partition'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--streaming-task-id'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--keep-alive-streams'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--keep-alive-all-streams'],
                help='Deprecated. Do nothing',
                hook=NoValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--report-only-stages'],
                help='Deprecated. Do nothing',
                hook=NoValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--report-to-ci'],
                help='Deprecated. Do nothing',
                hook=NoValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-topic'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-source-id'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-check-id'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-check-type'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-iteration-number'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-task-id-string'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--ci-logbroker-partition-group'],
                help='Deprecated. Do nothing',
                hook=SwallowValueDummyHook(),
                visible=False,
                deprecated=True,
            ),
        ]


class LocalConfOptions(Options):
    def __init__(self):
        self.use_local_conf = True
        self.local_conf_path = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--do-not-use-local-conf'],
                help='Do not use local configuration files',
                hook=SetConstValueHook('use_local_conf', False),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            EnvConsumer(
                'DO_NOT_USE_LOCAL_CONF',
                help='Do not use local configuration files',
                hook=SetConstValueHook('use_local_conf', False),
            ),
            ArgConsumer(
                ['--local-conf-path'],
                help='Path to <local.ymake>',
                hook=SetValueHook('local_conf_path'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            EnvConsumer('LOCAL_CONF_PATH', help='Path to <local.ymake>', hook=SetValueHook('local_conf_path')),
        ]


class BuildRootOptions(Options):
    def __init__(self, random_build_root=True):
        self.random_build_root = random_build_root
        self.custom_build_root = None
        self.limit_build_root_size = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--common-build-root'],
                help='Do not create random build root for each node',
                hook=SetConstValueHook('random_build_root', False),
                group=CHECKOUT_ONLY_GROUP,
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--build-root'],
                help='Set specific build root',
                hook=SetValueHook('custom_build_root'),
                group=CHECKOUT_ONLY_GROUP,
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--limit-build-root-size'],
                help='Set local limit of output node size',
                hook=SetConstValueHook('limit_build_root_size', True),
            ),
        ]


class ToolsOptions(Options):
    def __init__(self):
        self.tools_cache = False
        self.tools_cache_master = False
        self.build_cache = False
        self.build_cache_master = False
        self.tools_cache_bin = None
        self.tools_cache_ini = None

        self.tools_cache_conf = []
        self.tools_cache_conf_str = []
        self.build_cache_conf = []
        self.build_cache_conf_str = []
        self.tools_cache_gl_conf = []
        self.tools_cache_gl_conf_str = []
        self.tools_cache_size = 32212254720

    @staticmethod
    def consumer():
        tools_cache_size_hook = SetValueHook(
            name='tools_cache_size',
            transform=parse_size_arg,
            default_value=lambda x: '{}GiB'.format(str(parse_size_arg(x) / 1024 / 1024 / 1024)),
        )

        return [
            ArgConsumer(
                ['--ya-tc'],
                help='enable tools cache',
                hook=SetConstValueHook('tools_cache', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--noya-tc'],
                help='disable tools cache',
                hook=SetConstValueHook('tools_cache', False),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_TC',
                help='enable tools cache',
                hook=SetValueHook('tools_cache', return_true_if_enabled),
            ),
            ArgConsumer(
                ['--ya-ac'],
                help='enable build cache',
                hook=SetConstValueHook('build_cache', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_AC',
                help='enable build cache',
                hook=SetValueHook('build_cache', return_true_if_enabled),
            ),
            ArgConsumer(
                ['--ya-tc-master'],
                help='enable tools cache master mode',
                hook=SetConstValueHook('tools_cache_master', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ya-ac-master'],
                help='enable build cache master mode',
                hook=SetConstValueHook('build_cache_master', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ya-tc-bin'],
                help='Override tools cache binary',
                hook=SetValueHook('tools_cache_bin'),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_TC_BIN',
                hook=SetValueHook('tools_cache_bin'),
            ),
            ArgConsumer(
                ['--ya-tc-ini'],
                help='Override tools cache built-in ini-file',
                hook=SetValueHook('tools_cache_ini'),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ya-tc-conf'],
                help='Override configuration options',
                hook=SetAppendHook('tools_cache_conf_str'),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ya-ac-conf'],
                help='Override configuration options',
                hook=SetAppendHook('build_cache_conf_str'),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--ya-gl-conf'],
                help='Override configuration options',
                hook=SetAppendHook('tools_cache_gl_conf_str'),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--tools-cache-size'],
                help='Max tool cache size',
                hook=tools_cache_size_hook,
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ConfigConsumer('tools_cache'),
            ConfigConsumer('tools_cache_master'),
            ConfigConsumer('build_cache'),
            ConfigConsumer('build_cache_master'),
            ConfigConsumer('tools_cache_ini'),
            ConfigConsumer('tools_cache_conf', hook=ExtendHook('tools_cache_conf_str')),
            ConfigConsumer('build_cache_conf', hook=ExtendHook('build_cache_conf_str')),
            ConfigConsumer('tools_cache_gl_conf', hook=ExtendHook('tools_cache_gl_conf_str')),
            ConfigConsumer('tools_cache_size', hook=tools_cache_size_hook),
            EnvConsumer('YA_TOOLS_CACHE_SIZE', hook=tools_cache_size_hook),
        ]

    def postprocess(self):
        self.tools_cache_conf = []
        self.build_cache_conf = []
        self.tools_cache_gl_conf = []

        if not self.tools_cache:
            return

        self.tools_cache_conf = {k: v for k, v in (s.split('=', 1) for s in self.tools_cache_conf_str)}
        self.build_cache_conf = {k: v for k, v in (s.split('=', 1) for s in self.build_cache_conf_str)}
        self.tools_cache_gl_conf = {k: v for k, v in (s.split('=', 1) for s in self.tools_cache_gl_conf_str)}
        self._set_tools_cache_size()

    def _set_tools_cache_size(self):
        try:
            self.tools_cache_size = parse_size_arg(self.tools_cache_size)
        except (ValueError, InvalidSize):
            raise ArgsValidatingException(
                "tools_cache_size ({}) is not convertible to long".format(self.tools_cache_size)
            )

    def postprocess2(self, params):
        # Enable ya-tc in ya-bin executed indirectly, for example, through test_tool.
        if params.tools_cache:
            os.environ['YA_TC'] = '1'


class LocalCacheOptions(ToolsOptions):
    def __init__(self):
        super().__init__()

        self.strip_cache = False
        self.strip_symlinks = False
        self.symlinks_ttl = 7 * 24 * 60 * 60  # a week
        self.cache_stat = False
        self.cache_codec = None
        self.new_store = True
        self.new_runner = True
        self.new_store_ttl = 3 * 24 * 60 * 60  # 3 days
        self.cache_size = 300 * 1024 * 1024 * 1024
        self.auto_clean_results_cache = True

    @staticmethod
    def consumer():
        return ToolsOptions.consumer() + [
            ArgConsumer(
                ['--cache-stat'],
                help='Show cache statistics',
                hook=SetConstValueHook('cache_stat', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--gc'],
                help='Remove all cache except uids from the current graph',
                hook=SetConstValueHook('strip_cache', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--gc-symlinks'],
                help='Remove all symlink results except files from the current graph',
                hook=SetConstValueHook('strip_symlinks', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--symlinks-ttl'],
                help='Results cache TTL',
                hook=SetValueHook(
                    'symlinks_ttl',
                    transform=parse_timespan_arg,
                    default_value=lambda x: '{}h'.format(str(parse_timespan_arg(x) * 1.0 / 60 / 60)),
                ),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--cache-size'],
                help='Max cache size',
                hook=SetValueHook(
                    'cache_size',
                    transform=parse_size_arg,
                    default_value=lambda x: '{}GiB'.format(str(parse_size_arg(x) / 1024 / 1024 / 1024)),
                ),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--cache-codec'],
                help='Cache codec',
                hook=SetValueHook('cache_codec'),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--auto-clean'],
                help='Auto clean results cache',
                hook=SetValueHook('auto_clean_results_cache', transform=return_true_if_enabled),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--new-store'],
                help='Try alternative storage',
                hook=SetConstValueHook('new_store', True),
                group=CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_NEW_STORE3',
                help='Use alternative storage',
                hook=SetConstValueHook('new_store', True),
            ),
            ArgConsumer(
                ['--new-runner'],
                help='Try alternative runner',
                hook=SetConstValueHook('new_runner', True),
                group=FEATURES_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            EnvConsumer(
                'YA_NEW_RUNNER3',
                help='Use alternative runner',
                hook=SetConstValueHook('new_runner', True),
            ),
            ConfigConsumer('new_store'),
            ConfigConsumer('new_runner'),
            ConfigConsumer('new_store_ttl', help='Cache TTL in seconds', group=DEVELOPERS_OPT_GROUP),
            ConfigConsumer('symlinks_ttl', help='Results cache TTL in seconds', group=DEVELOPERS_OPT_GROUP),
            ConfigConsumer('cache_codec', help='Use specific codec for cache', group=DEVELOPERS_OPT_GROUP),
            ConfigConsumer('cache_size'),
            EnvConsumer('YA_CACHE_SIZE', hook=SetValueHook('cache_size')),
            ConfigConsumer('auto_clean_results_cache'),
            ConfigConsumer('strip_symlinks'),
        ]

    def postprocess(self):
        super().postprocess()
        self._set_cache_size()
        self._set_ttl('new_store_ttl', 3 * 24 * 60 * 60)
        self._set_ttl('symlinks_ttl', 7 * 24 * 60 * 60)

    def _set_cache_size(self):
        try:
            self.cache_size = parse_size_arg(self.cache_size)
        except (ValueError, InvalidSize):
            raise ArgsValidatingException("cache_size ({}) is not convertible to long".format(self.cache_size))

    def _set_ttl(self, attr, default):
        attr_val = getattr(self, attr)
        if attr_val is None:
            setattr(self, attr, default)
            return

        try:
            setattr(self, attr, parse_timespan_arg(attr_val))
        except (ValueError, InvalidTimespan):
            raise ArgsValidatingException("{} ({}) is not convertible to seconds".format(attr, attr_val))


class DistCacheSetupOptions(LocalCacheOptions):
    def __init__(self):
        super().__init__()

        if app_config.in_house:
            self.yt_proxy = 'markov.yt.yandex.net'
            self.yt_dir = '//home/devtools-cache'
        else:
            self.yt_proxy = ''
            self.yt_dir = ''

        self.yt_token = None
        self.yt_token_path = '~/.yt/token'
        self.yt_proxy_role = None
        self.yt_readonly = True
        self.yt_max_cache_size = None
        self.yt_store_ttl = None

    @staticmethod
    def consumer():
        return LocalCacheOptions.consumer() + [
            ArgConsumer(
                ['--yt-proxy'],
                help='YT storage proxy',
                hook=SetValueHook('yt_proxy'),
                group=YT_CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--yt-dir'],
                help='YT storage cypress directory pass',
                hook=SetValueHook('yt_dir'),
                group=YT_CACHE_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--yt-token'],
                help='YT token',
                hook=SetValueHook('yt_token', default_value=lambda _: '[HIDDEN]'),
                group=YT_CACHE_PUT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            ArgConsumer(
                ['--yt-token-path'],
                help='YT token path',
                hook=SetValueHook('yt_token_path'),
                group=YT_CACHE_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--yt-proxy-role'],
                help='YT proxy role',
                hook=SetValueHook('yt_proxy_role'),
                group=YT_CACHE_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--yt-put'],
                help='Upload to YT store',
                hook=SetConstValueHook('yt_readonly', False),
                group=YT_CACHE_PUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--yt-max-store-size'],
                help='YT storage max size',
                hook=SetValueHook('yt_max_cache_size'),
                group=YT_CACHE_PUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--yt-store-ttl'],
                help='YT store ttl in hours',
                hook=SetValueHook('yt_store_ttl', transform=int),
                group=YT_CACHE_PUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer(
                'YA_YT_PROXY',
                help='YT storage proxy',
                hook=SetValueHook('yt_proxy'),
            ),
            EnvConsumer(
                'YA_YT_DIR',
                help='YT storage cypress directory pass',
                hook=SetValueHook('yt_dir'),
            ),
            EnvConsumer(
                'YA_YT_TOKEN',
                help='YT token',
                hook=SetValueHook('yt_token'),
            ),
            EnvConsumer(
                'YA_YT_TOKEN_PATH',
                help='YT token path',
                hook=SetValueHook('yt_token_path'),
            ),
            EnvConsumer(
                'YA_YT_PROXY_ROLE',
                help='YT proxy role',
                hook=SetValueHook('yt_proxy_role'),
            ),
            EnvConsumer(
                'YA_YT_PUT',
                help='Upload to YT store',
                hook=SetConstValueHook('yt_readonly', False),
            ),
            EnvConsumer(
                'YA_YT_MAX_STORAGE_SIZE',
                help='YT storage max size',
                hook=SetValueHook('yt_max_cache_size'),
            ),
            EnvConsumer(
                'YA_YT_STORE_TTL',
                help='YT store ttl in hours',
                hook=SetValueHook('yt_store_ttl'),
            ),
            ConfigConsumer('yt_proxy'),
            ConfigConsumer('yt_dir'),
            ConfigConsumer('yt_token_path'),
            ConfigConsumer('yt_proxy_role'),
            ConfigConsumer('yt_max_cache_size'),
            ConfigConsumer('yt_store_ttl'),
            ConfigConsumer('yt_readonly'),
        ]

    def postprocess(self):
        super().postprocess()
        if self.yt_token_path:
            self.yt_token_path = os.path.expanduser(self.yt_token_path)
        self._read_token_file()
        try:
            self.yt_max_cache_size = parse_yt_max_cache_size(self.yt_max_cache_size)
        except ValueError as e:
            raise ArgsValidatingException(f"Wrong yt_max_cache_size value {self.yt_max_cache_size}: {e!s}")

    def postprocess2(self, params):
        super().postprocess2(params)
        if params.yt_proxy.startswith('hahn') and params.yt_dir == '//home/devtools/cache' and params.yt_store:
            logger.warning(
                "Attempt to use the obsolete YT-store (hahn://home/devtools/cache). Please, remove incorrect settings from all ya.conf files or command line parameters"
            )
            params.yt_proxy = 'markov.yt.yandex.net'
            params.yt_dir = '//home/devtools-cache'

    def _read_token_file(self):
        if self.yt_token:
            # Do not nullify token path: it is used in coverage, etc.
            # self.yt_token_path = None
            return

        try:
            with open(self.yt_token_path, 'rb') as f:
                token = f.read().decode('utf-8-sig').strip()
                token = six.ensure_str(token)
                if not token:
                    return

                self.yt_token = token
                logger.debug("Load yt token from %s", self.yt_token_path)
        except UnicodeDecodeError:
            logger.warning('Incorrect file "{}" encoding. Utf8 is expected'.format(self.yt_token_path))
            self.yt_token_path = None
        except Exception:
            self.yt_token_path = None


class DistCacheOptions(DistCacheSetupOptions):
    def __init__(self):
        super().__init__()

        # XXX see YA-1354
        self.dist_cache_evict_binaries = False
        self.dist_cache_evict_bundles = False
        self.dist_cache_evict_cached = False
        self.dist_cache_evict_test_runs = False
        self.dist_cache_max_file_size = 0
        self.dist_store_threads = min(get_cpu_count() * 2, get_cpu_count() + 12)
        self.dist_cache_late_fetch = False
        self.yt_store = True if app_config.in_house else False  # should be false for opensource
        self.yt_create_tables = False
        self.yt_self_uid = False
        self.yt_store_codec = None
        self.yt_replace_result = False
        self.yt_replace_result_yt_upload_only = False
        self.yt_replace_result_add_objects = False
        self.yt_store_exclusive = False
        self.yt_store_threads = max(get_cpu_count() // 2, 1)
        self.yt_store_wt = True
        self.yt_store_refresh_on_read = False
        self.yt_store_cpp_client = True
        self.yt_store_cpp_prepare_data = False
        self.yt_store_probe_before_put = False
        self.yt_store_probe_before_put_min_size = 0
        self.yt_store_retry_time_limit = None
        self.yt_store_init_timeout = None
        self.yt_store_prepare_timeout = None
        self.yt_store_crit = None
        self.bazel_remote_store = False
        self.bazel_remote_baseuri = 'http://[::1]:8080/'
        self.bazel_remote_username = None
        self.bazel_remote_password = None
        self.bazel_remote_password_file = None
        self.bazel_remote_readonly = True

    @staticmethod
    def consumer():
        return (
            DistCacheSetupOptions.consumer()
            + [
                ArgConsumer(
                    ['--dist-cache-evict-bins'],
                    help='Remove all non-tool binaries from build results. Works only with --bazel-remote-put mode',
                    hook=SetConstValueHook('dist_cache_evict_binaries', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('dist_cache_evict_binaries'),
                ArgConsumer(
                    ['--dist-cache-evict-bundles'],
                    help='Remove all bundles from build results. Works only with --bazel-remote-put mode',
                    hook=SetConstValueHook('dist_cache_evict_bundles', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('dist_cache_evict_bundles'),
                ArgConsumer(
                    ['--dist-cache-evict-cached'],
                    help="Don't build or download build results if they are present in the dist cache",
                    hook=SetConstValueHook('dist_cache_evict_cached', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('dist_cache_evict_cached'),
                ArgConsumer(
                    ['--dist-cache-evict-test-runs'],
                    help="Remove all test_runs from build results. Works only with --bazel-remote-put mode",
                    hook=SetConstValueHook('dist_cache_evict_test_runs', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('dist_cache_evict_test_runs'),
                ArgConsumer(
                    ['--dist-store-threads'],
                    help='dist store max threads',
                    hook=SetValueHook('dist_store_threads', transform=int),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ArgConsumer(
                    ['--dist-cache-max-file-size'],
                    help='Sets the maximum size in bytes of a single file stored in the dist cache. Use 0 for no limit',
                    hook=SetValueHook('dist_cache_max_file_size', transform=int),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                ConfigConsumer('dist_cache_max_file_size'),
                ArgConsumer(
                    ['--dist-cache-late-fetch'],
                    help="Mode with delayed probing of a remote store, to increase the dist hit cache",
                    hook=SetConstValueHook('dist_cache_late_fetch', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.INTERNAL,
                ),
                ConfigConsumer('dist_cache_late_fetch'),
                ArgConsumer(
                    ['--bazel-remote-store'],
                    help='Use Bazel-remote storage',
                    hook=SetConstValueHook('bazel_remote_store', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ArgConsumer(
                    ['--no-bazel-remote-store'],
                    help='Disable Bazel-remote storage',
                    hook=SetConstValueHook('bazel_remote_store', False),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('bazel_remote_store'),
                ArgConsumer(
                    ['--bazel-remote-base-uri'],
                    help='Bazel-remote base URI',
                    hook=SetValueHook('bazel_remote_baseuri'),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('bazel_remote_baseuri'),
                ArgConsumer(
                    ['--bazel-remote-username'],
                    help='Bazel-remote username',
                    hook=SetValueHook('bazel_remote_username'),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('bazel_remote_username'),
                ArgConsumer(
                    ['--bazel-remote-password'],
                    help='Bazel-remote password',
                    hook=SetValueHook('bazel_remote_password'),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ArgConsumer(
                    ['--bazel-remote-password-file'],
                    help='Bazel-remote password file',
                    hook=SetValueHook('bazel_remote_password_file'),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ConfigConsumer('bazel_remote_password_file'),
                ArgConsumer(
                    ['--bazel-remote-put'],
                    help='Upload to Bazel-remote store',
                    hook=SetConstValueHook('bazel_remote_readonly', False),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                ArgConsumer(
                    ['--yt-store'],
                    help='Use YT storage',
                    hook=SetConstValueHook('yt_store', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                ArgConsumer(
                    ['--no-yt-store'],
                    help='Disable YT storage',
                    hook=SetConstValueHook('yt_store', False),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.BASIC,
                ),
                ArgConsumer(
                    ['--yt-write-through'],
                    help='Populate local cache while updating YT store',
                    hook=SetConstValueHook('yt_store_wt', True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                ArgConsumer(
                    ['--no-yt-write-through'],
                    help='Don\'t populate local cache while updating YT store',
                    hook=SetConstValueHook('yt_store_wt', False),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                ArgConsumer(
                    ['--yt-create-tables'],
                    help='Create YT storage tables (DEPRECATED. Use "ya cache yt create-tables")',
                    hook=SetConstValueHook('yt_create_tables', True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                    deprecated=True,
                ),
                ArgConsumer(
                    ['--yt-self-uid'],
                    help='Include self_uid in YT store metadata (DEPRECATED. Use "ya cache yt create-tables --version 3")',
                    hook=SetConstValueHook('yt_self_uid', True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                    deprecated=True,
                ),
                ArgConsumer(
                    ['--yt-store-filter'],
                    help='YT store filter (DEPRECATED. Do nothing)',
                    hook=SwallowValueDummyHook(),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.NONE,
                    deprecated=True,
                ),
                ArgConsumer(
                    ['--yt-store-codec'],
                    help='YT store codec',
                    hook=SetValueHook('yt_store_codec'),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                ArgConsumer(
                    ['--yt-store-threads'],
                    help='YT store max threads',
                    hook=SetValueHook('yt_store_threads', transform=int),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.ADVANCED,
                ),
                EnvConsumer(
                    'YA_YT_STORE4',
                    help='Use YT storage',
                    hook=SetValueHook('yt_store', transform=return_true_if_enabled),
                ),
                EnvConsumer(
                    'YA_YT_WT',
                    help='Populate local cache while updating YT store',
                    hook=SetValueHook('yt_store_wt', transform=return_true_if_enabled),
                ),
                EnvConsumer(
                    'YA_YT_CREATE_TABLES',
                    help='Create YT storage tables',
                    hook=SetConstValueHook('yt_create_tables', True),
                ),
                EnvConsumer('YA_YT_STORE_CODEC', help='YT store codec', hook=SetValueHook('yt_store_codec')),
                EnvConsumer(
                    'YA_YT_STORE_THREADS',
                    help='YT store max threads',
                    hook=SetValueHook('yt_store_threads', transform=int),
                ),
                ConfigConsumer('yt_store'),
                ConfigConsumer('yt_store_wt'),
                ConfigConsumer('yt_store_threads'),
                ConfigConsumer('yt_store_codec'),
                ArgConsumer(
                    ['--yt-store-exclusive'],
                    help='Use YT storage exclusively (fail build if required data is not presented in the YT store)',
                    hook=SetConstValueHook('yt_store_exclusive', True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                EnvConsumer('YA_YT_STORE_EXCLUSIVE', hook=SetValueHook('yt_store_exclusive')),
                ArgConsumer(
                    ['--yt-store-refresh-on-read'],
                    help='On read mark cache items as fresh (simulate LRU)',
                    hook=SetConstValueHook('yt_store_refresh_on_read', True),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.INTERNAL,
                ),
                ConfigConsumer('yt_store_refresh_on_read'),
                EnvConsumer('YA_YT_STORE_CPP_CLIENT', hook=SetValueHook('yt_store_cpp_client', return_true_if_enabled)),
                ConfigConsumer('yt_store_cpp_client'),
                EnvConsumer(
                    'YA_YT_STORE_CPP_PREPARE_DATA',
                    hook=SetValueHook('yt_store_cpp_prepare_data', return_true_if_enabled),
                ),
                ConfigConsumer('yt_store_cpp_prepare_data'),
                EnvConsumer(
                    'YA_YT_STORE_REFRESH_ON_READ', hook=SetValueHook('yt_store_refresh_on_read', return_true_if_enabled)
                ),
            ]
            + make_opt_consumers(
                'yt_replace_result',
                help='Build only targets that need to be uploaded to the YT store',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                ),
            )
            + make_opt_consumers(
                'yt_replace_result_add_objects',
                help='Tune yt-replace-result option: add objects (.o) files to build results. Useless without --yt-replace-result',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                ),
            )
            + make_opt_consumers(
                'yt_replace_result_rm_binaries',
                help='Tune yt-replace-result option: remove all non-tool binaries from build results. Useless without --yt-replace-result',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook('dist_cache_evict_binaries', True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook('dist_cache_evict_binaries', True),
                ),
            )
            + make_opt_consumers(
                'yt_replace_result_rm_bundles',
                help='Tune yt-replace-result option: remove all bundles from build results. Useless without --yt-replace-result',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook('dist_cache_evict_bundles', True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook('dist_cache_evict_bundles', True),
                ),
            )
            + make_opt_consumers(
                'yt_replace_result_yt_upload_only',
                help='Tune yt-replace-result option: put only yt upload nodes into results. Useless without --yt-replace-result',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                ),
            )
            + make_opt_consumers(
                'yt_store_probe_before_put',
                help='Probe uid in a YT store before put results into it. Useless without --yt-put',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                ),
            )
            + make_opt_consumers(
                'yt_store_probe_before_put_min_size',
                help='Don\'t probe a YT store if size of data less than specified. Useless without --yt-store-probe-before-put',
                arg_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                    group=YT_CACHE_PUT_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetConstValueHook(n, True),
                ),
            )
            + make_opt_consumers(
                'yt_store_retry_time_limit',
                help='Maximum duration of YT method execution attempts',
                arg_opts=dict(
                    hook=lambda n: SetValueHook(n, transform=float),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetValueHook(n, transform=float),
                ),
            )
            + [
                ArgConsumer(
                    ['--yt-store2'],
                    help='Deprecated. Do nothing',
                    hook=NoValueDummyHook(),
                    visible=False,
                    deprecated=True,
                ),
                ArgConsumer(
                    ['--no-yt-store2'],
                    help='Deprecated. Do nothing',
                    hook=NoValueDummyHook(),
                    visible=False,
                    deprecated=True,
                ),
            ]
            + make_opt_consumers(
                'yt_store_init_timeout',
                help='Maximum duration of store initialization',
                arg_opts=dict(
                    hook=lambda n: SetValueHook(n, transform=float),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetValueHook(n, transform=float),
                ),
                cfg_opts={},
            )
            + make_opt_consumers(
                'yt_store_prepare_timeout',
                help='Maximum duration of metadata reading',
                arg_opts=dict(
                    hook=lambda n: SetValueHook(n, transform=float),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetValueHook(n, transform=float),
                ),
                cfg_opts={},
            )
            + make_opt_consumers(
                'yt_store_crit',
                help='Break execution if YT store fails ("get" - if get fails, "put" - if either get or put fails)',
                arg_opts=dict(
                    hook=lambda n: SetValueHook(n, values=["get", "put"]),
                    group=YT_CACHE_CONTROL_GROUP,
                    visible=HelpLevel.EXPERT,
                ),
                env_opts=dict(
                    hook=lambda n: SetValueHook(n, values=["get", "put"]),
                ),
            )
        )

    def postprocess(self):
        super().postprocess()
        if self.yt_store_exclusive:
            self.yt_store = True

        if self.yt_store_threads == 0 and self.yt_store:
            raise ArgsValidatingException('YT storage is enabled but YT store max threads is set to 0')

        if not self.yt_store:
            self.yt_store_threads = 0

        # YA-1354: The plan is to unify YT and Bazel store interfaces
        # presently dist_store_threads is only a Bazel's option
        if self.dist_store_threads == 0 and self.bazel_remote_store:
            raise ArgsValidatingException('Bazel remote storage is enabled but dist store max threads is set to 0')

        if not self.bazel_remote_store:
            self.dist_store_threads = 0

        if self.yt_store:
            if not self.yt_proxy:
                raise ArgsValidatingException('YT storage is enabled but YT proxy is not set')
            if not self.yt_dir:
                raise ArgsValidatingException('YT storage is enabled but YT dir is not set')

        if self.yt_store_exclusive and self.yt_store_crit is None:
            self.yt_store_crit = "get"

        if not self.yt_store_wt:
            self.yt_store_crit = "put"

        if self.yt_readonly and self.yt_store_crit == "put":
            self.yt_store_crit = "get"


class JavaSpecificOptions(Options):
    def __init__(self):
        self.get_deps = None
        self.dump_sources = False

        # maven-export
        self.export_to_maven = False
        self.version = None
        self.deploy = False
        self.repository_id = None
        self.repository_url = None
        self.maven_settings = None
        self.maven_output = None
        self.javac_flags = {}
        self.error_prone_flags = []
        self.maven_no_recursive_deps = False
        self.maven_exclude_transitive_deps = False
        self.disable_scriptgen = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--get-deps'],
                help='Compile and collect all dependencies into specified directory',
                hook=SetValueHook('get_deps'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['-s', '--sources'],
                help='Make sources jars as well',
                hook=SetConstValueHook('dump_sources', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--maven-export'],
                help='Export to maven repository',
                hook=SetConstValueHook('export_to_maven', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--maven-no-recursive-deps'],
                help='Not export recursive dependencies',
                hook=SetConstValueHook('maven_no_recursive_deps', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--maven-exclude-transitive-from-deps'],
                help='Exclude transitive from dependencies',
                hook=SetConstValueHook('maven_exclude_transitive_deps', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--version'],
                help="Version of artifacts for exporting to maven",
                hook=SetValueHook('version'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--deploy'],
                help='Deploy artifact to repository',
                hook=SetConstValueHook('deploy', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--repository-id'],
                help="Maven repository id",
                hook=SetValueHook('repository_id'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--repository-url'],
                help="Maven repository url",
                hook=SetValueHook('repository_url'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--settings'],
                help="Maven settings.xml file path",
                hook=SetValueHook('maven_settings'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--maven-out-dir'],
                help="Maven output directory( for .class files )",
                hook=SetValueHook('maven_output'),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--javac-opts', '-J'],
                help='Set common javac flags (name=val)',
                hook=DictPutHook('javac_flags', None),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--error-prone-flags'],
                help='Set error prone flags',
                hook=SetAppendHook('error_prone_flags', None),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--disable-run-script-generation'],
                help='Disable run.sh(run.bat) scripts generation for JAVA_PROGRAM',
                hook=SetConstValueHook('disable_scriptgen', True),
                group=JAVA_BUILD_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
        ]

    def postprocess2(self, params):
        if self.export_to_maven:
            params.flags['MAVEN_EXPORT'] = 'yes'
            if self.deploy:
                params.flags['MAVEN_DEPLOY'] = 'yes'
                if not self.repository_url:
                    raise ArgsValidatingException('Maven repository url is not set')
                if not self.repository_id:
                    raise ArgsValidatingException('Maven repository id is not set')
                params.flags['MAVEN_REPO_URL'] = self.repository_url
                params.flags['MAVEN_REPO_ID'] = self.repository_id
            params.flags['MAVEN_EXPORT_OUT_DIR'] = '' if self.maven_output is None else self.maven_output
            if self.version is not None:
                params.flags['MAVEN_EXPORT_VERSION'] = self.version
            params.flags['MAVEN_EXPORT_SETTINGS'] = '' if self.maven_settings is None else self.maven_settings
            params.flags['MAVEN_LOCAL_PEERS'] = 'yes' if self.maven_no_recursive_deps else 'no'
            params.flags['MAVEN_EXCLUDE_TRANSITIVE_PEERS'] = 'yes' if self.maven_exclude_transitive_deps else 'no'
        if self.disable_scriptgen:
            params.flags['DISABLE_SCRIPTGEN'] = 'yes'
        if self.dump_sources:
            params.flags['SOURCES_JAR'] = 'yes'


class ConfigurationPresetsOptions(Options):
    def __init__(self):
        self.preset_mapsmobi = False
        self.preset_with_credits = False
        self.preset_disable_customization = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--maps-mobile'],
                help='Enable mapsmobi configuration preset',
                hook=SetConstValueHook('preset_mapsmobi', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--with-credits'],
                help='Enable CREDITS file generation',
                hook=SetConstValueHook('preset_with_credits', True),
                group=OUTPUT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--disable-customization'],
                help='Disable ya make customozation',
                hook=SetConstValueHook('preset_disable_customization', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_DISABLE_CUSTOMIZATION',
                hook=SetConstValueHook('preset_disable_customization', True),
            ),
            ConfigConsumer(
                'disable_customization',
                hook=SetConstValueHook('preset_disable_customization', True),
            ),
        ]

    def postprocess2(self, params):
        if self.preset_disable_customization:
            params.flags['DISABLE_YMAKE_CONF_CUSTOMIZATION'] = 'yes'
            params.host_flags['DISABLE_YMAKE_CONF_CUSTOMIZATION'] = 'yes'


class YaThreadsOptions(Options):
    def __init__(self):
        self.ya_threads = get_cpu_count()

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-y', '--ya-threads'],
                help='Ya threads count',
                hook=SetValueHook('ya_threads', int),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


class YMakeRetryOptions(Options):
    def __init__(self):
        self.no_caches_on_retry = False
        self.no_ymake_retry = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--no-ymake-caches-on-retry'],
                help='Do not use ymake caches on retry',
                hook=SetConstValueHook('no_caches_on_retry', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                'YA_NO_YMAKE_CACHES_ON_RETRY',
                help='Do not use ymake caches on retry',
                hook=SetValueHook('no_caches_on_retry', return_true_if_enabled),
            ),
            ConfigConsumer(
                'no_ymake_caches_on_retry',
                help='Do not use ymake caches on retry',
                hook=SetValueHook('no_caches_on_retry'),
            ),
            ArgConsumer(
                ['--no-ymake-retry'],
                help='Do not retry ymake',
                hook=SetConstValueHook('no_ymake_retry', True),
                group=ADVANCED_OPT_GROUP,
                visible=False,
            ),
            EnvConsumer(
                'YA_NO_YMAKE_RETRY',
                help='Do not retry ymake',
                hook=SetValueHook('no_ymake_retry', return_true_if_enabled),
            ),
        ]


class CompressYmakeOutputOptions(Options):
    def __init__(self):
        self.compress_ymake_output = False
        self.compress_ymake_output_codec = "zstd08_1"

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--compress-ymake-output'],
                help='Compress ymake output to reduce max memory usage',
                hook=SetConstValueHook('compress_ymake_output', True),
                group=GRAPH_GENERATION_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer('compress_ymake_output'),
            EnvConsumer('YA_COMPRESS_YMAKE_OUTPUT', hook=SetValueHook('compress_ymake_output', return_true_if_enabled)),
            ArgConsumer(
                ['--no-compress-ymake-output'],
                help='Disable ymake output compressing',
                hook=SetConstValueHook('compress_ymake_output', False),
                group=GRAPH_GENERATION_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--compress-ymake-output-codec'],
                help='Codec to compress ymake output with',
                hook=SetValueHook('compress_ymake_output_codec', str),
                group=GRAPH_GENERATION_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ConfigConsumer('compress_ymake_output_codec'),
            EnvConsumer('YA_COMPRESS_YMAKE_OUTPUT_CODEC', hook=SetValueHook('compress_ymake_output_codec', str)),
        ]


def ya_make_options(  # compat
    free_build_targets=False,
    use_distbuild=False,
    test_size_filters=None,
    cache_tests=False,
    random_build_root=True,
    warn_mode=None,
    test_console_report=True,
    run_tests=0,
    run_tests_size=1,
    is_ya_test=False,
    strip_idle_build_results=False,
    build_type='debug',
):
    return (
        [
            ShowHelpOptions(),
            BuildTypeOptions(build_type=build_type),
            FlagsOptions(),
            CustomFetcherOptions(),
            UniversalFetcherOptions(),
            RebuildOptions(),
            YndexerOptions(),
            StrictInputsOptions(),
            DefaultNodeRequirementsOptions(),
            DumpReportOptions(),
            BuildTargetsOptions(with_free=free_build_targets),
            PythonBuildOptions(),
            ContinueOnFailOptions(),
            PrintStatisticsOptions(),
            BeVerboseOptions(),
            DetailedArgsOptions(),
            OutputStyleOptions(),
            ProfileOptions(),
            CustomSourceRootOptions(),
            CustomBuildRootOptions(),
            CustomMiscBuildInfoDirOptions(),
            YMakeDebugOptions(),
            ConfigureDebugOptions(),
            YMakeBinOptions(),
            YMakeRetryOptions(),
            YMakeModeOptions(),
            LocalConfOptions(),
            BuildRootOptions(random_build_root),
            BuildThreadsOptions(build_threads=None),
            CustomGraphAndContextOptions(),
            IgnoreNodesExitCode(),
            YMakeDumpGraphOptions(),
            CrossCompilationOptions(),
            GraphFilterOutputResultOptions(),
            GraphOperateResultsOptions(),
            YaMakeOptions(),
            ExecutorOptions(),
            ForceDependsOptions(),
            IgnoreRecursesOptions(),
            CreateSymlinksOptions(),
            SetNiceValueOptions(),
            ContentUidsOptions(),
        ]
        + test_opts.test_options(
            cache_tests,
            test_size_filters=test_size_filters,
            test_console_report=test_console_report,
            run_tests=run_tests,
            run_tests_size=run_tests_size,
            is_ya_test=is_ya_test,
            strip_idle_build_results=strip_idle_build_results,
        )
        + [
            CommonUploadOptions(),
            AuthOptions(),
            SandboxUploadOptions(),  # must go after AuthOptions because it inherits from it
            MDSUploadOptions(),
            SonarOptions(),
            OutputOptions(),
            ArcPrefetchOptions(),
            GenerateLegacyDirOptions(),
            InstallDirOptions(),  # temp legacy option
            KeepTempsOptions(),
            HtmlDisplayOptions(),
            TeamcityOptions(),
            ProfilerOptions(),
            LogFileOptions(),
            EventLogFileOptions(),
            StdoutOptions(),
            DistCacheOptions(),
            YWarnModeOptions(warn_mode),
            JavaSpecificOptions(),
            PGOOptions(),
            PICOptions(),
            TerminalProfileOptions(),
            MiniYaOpts(),
            TestenvReportDirOptions(),
            StreamReportOptions(),
            RawParamsOptions(),
            ConfigurationPresetsOptions(),
            YaThreadsOptions(),
            DumpDebugCommonOptions(),
            DumpDebugOptions(),
            CompressYmakeOutputOptions(),
            YaBin3Options(),
        ]
        + distbs_options(use_distbuild=use_distbuild)
        + checkout_options()
        + svn_checkout_options()
        + build_graph_cache_config_opts()
    )


def checkout_options():
    try:
        from devtools.ya.build.build_opts import checkout
    except ImportError:
        return []
    return [
        checkout.CheckoutOptions(),
    ]


def svn_checkout_options():
    try:
        from devtools.ya.build.build_opts import checkout
    except ImportError:
        return []
    return [
        checkout.SvnCheckoutOptions(),
    ]


def distbs_options(use_distbuild=False):
    mock_distbs = False
    try:
        from devtools.ya.build.build_opts import distbs
    except ImportError:
        mock_distbs = True

    if mock_distbs:

        class DistbsOptions(Options):
            def __init__(self):
                self.use_distbuild = False
                self.json_prefix = None
                self.share_results = False
                self.graph_stat_path = None
                self.dump_graph_execution_cost = False
                self.download_artifacts = False
                self.upload_to_remote_store = False

        return [
            DistbsOptions(),
        ]
    else:
        return [
            distbs.BuildMethodOptions(use_distbuild=use_distbuild),
            distbs.RemoteGenGraphOptions(),
            distbs.DistbsOptions(),
        ]


def build_graph_cache_config_opts():
    mock_build_graph_cache = False
    try:
        from devtools.ya.build.build_opts import build_graph_cache
    except ImportError:
        mock_build_graph_cache = True

    if mock_build_graph_cache:

        class BuildGraphCacheConfigOptions(Options):
            def __init__(self):
                self.build_graph_cache_heater = False
                self.build_graph_cache_dir = None
                self.build_graph_cache_archive = None

                self.build_graph_cache_cl = None
                self.build_graph_cache_trust_cl = False
                # or DistbsOptions.distbuild_patch
                self.build_graph_cache_cl_for_resource = False

                # Build graph cache specification
                self.build_graph_autocheck_params = None
                self.build_graph_autocheck_params_str = None
                self.build_graph_cache_resource = None

                self.build_graph_cache_from_sb = False

                self.build_graph_source_root_pattern = 'SOURCE_ROOT'
                self.build_graph_result_dir = None
                self.build_graph_arc_server = 'arc-vcs.yandex-team.ru:5623'
                self.build_graph_public_arc_server = 'arc-devtools.arc-vcs.yandex-team.ru:6734'
                self.build_graph_use_ymake_cache_params = None
                self.build_graph_use_ymake_cache_params_str = None

                self.build_graph_cache_use_arc_bin = False
                self.build_graph_cache_arc_bin = None

        return [BuildGraphCacheConfigOptions()]

    else:
        return [build_graph_cache.BuildGraphCacheConfigOptions()]
