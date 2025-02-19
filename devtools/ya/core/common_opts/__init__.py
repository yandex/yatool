import logging
import os

import six


import devtools.ya.core.stages_profiler
from devtools.ya.core import config
from devtools.ya.core import profiler
from devtools.ya.core.yarg.groups import (
    OPERATIONAL_CONTROL_GROUP,
    CHECKOUT_ONLY_GROUP,
    OUTPUT_CONTROL_GROUP,
    PRINT_CONTROL_GROUP,
    PLATFORM_CONFIGURATION_GROUP,
    GRAPH_GENERATION_GROUP,
    FEATURES_GROUP,
    COMMON_UPLOAD_OPT_GROUP,
)
from devtools.ya.core.yarg.help_level import HelpLevel
from devtools.ya.core.yarg import (
    Options,
    ArgConsumer,
    ConfigConsumer,
    SetValueHook,
    SetConstValueHook,
    DEVELOPERS_OPT_GROUP,
    BULLET_PROOF_OPT_GROUP,
    ADVANCED_OPT_GROUP,
    EnvConsumer,
    SetAppendHook,
    DictPutHook,
    DictUpdateHook,
    ArgsValidatingException,
    SANDBOX_UPLOAD_OPT_GROUP,
    AUTH_OPT_GROUP,
    return_true_if_enabled,
    ExtendHook,
    BaseHook,
)

# TODO: Fix imports everywhere
from devtools.ya.core.yarg import ShowHelpOptions  # noqa: F401

import exts.func
import exts.path2

import devtools.ya.test.const
import devtools.ya.core.config as cc

import yalibrary.upload.consts
from yalibrary.platform_matcher import is_darwin_rosetta


logger = logging.getLogger(__name__)


class BuildTypeConsumer(ArgConsumer):
    def __init__(self, names, option, short_help, visible):
        super(BuildTypeConsumer, self).__init__(
            names,
            help=short_help + ' https://docs.yandex-team.ru/ya-make/usage/ya_make/#build-type',
            hook=SetValueHook(
                option,
                values=[
                    'debug',
                    'release',
                    'profile',
                    'gprof',
                    'valgrind',
                    'valgrind-release',
                    'coverage',
                    'relwithdebinfo',
                    'minsizerel',
                    'debugnoasserts',
                    'fastdebug',
                ],
                transform=lambda s: s.lower(),
            ),
            group=PLATFORM_CONFIGURATION_GROUP,
            visible=visible,
        )


class TransportOptions(Options):
    def __init__(self):
        self.transport = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--skynet'],
                help='Should upload using skynet',
                hook=SetConstValueHook('transport', 'skynet'),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--http'],
                help='Should upload using http (deprecated)',
                hook=SetConstValueHook('transport', 'http'),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
            ArgConsumer(
                ['--sandbox-mds'],
                help='Should upload using MDS as storage (default)',
                hook=SetConstValueHook('transport', 'mds'),
                group=SANDBOX_UPLOAD_OPT_GROUP,
            ),
        ]


class BeVerboseOptions(Options):
    def __init__(self, be_verbose=False):
        self.be_verbose = be_verbose

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['-v', '--verbose'],
            help='Be verbose',
            hook=SetConstValueHook('be_verbose', True),
            group=PRINT_CONTROL_GROUP,
            visible=HelpLevel.BASIC,
        )


class DetailedArgsOptions(Options):
    def __init__(self, detailed_args=False):
        self.detailed_args = detailed_args

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--detailed-args'],
            help='Detailed args in envlog',
            hook=SetConstValueHook('detailed_args', True),
            group=PRINT_CONTROL_GROUP,
            visible=HelpLevel.BASIC,
        )


class OutputStyleOptions(Options):
    def __init__(self):
        self.do_not_output_stderrs = False
        self.output_style = 'ninja'
        self.mask_roots = None
        self.status_refresh_interval = 0.1
        self.do_emit_status = True
        self.do_not_emit_nodes = []
        self.use_roman_numerals = False

    @staticmethod
    def additional_style_opts():
        return [
            ArgConsumer(
                ['-T'],
                help='Do not rewrite output information (ninja/make)',
                hook=SetConstValueHook('output_style', 'make'),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
        ]

    @staticmethod
    def common_style_opts():
        return [
            ArgConsumer(
                ['--no-emit-status'],
                help='Do not emit status',
                hook=SetConstValueHook('do_emit_status', False),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--no-emit-nodes'],
                help='Do emit result nodes with type',
                hook=SetAppendHook('do_not_emit_nodes'),
                group=PRINT_CONTROL_GROUP,
                visible=False,
                deprecated=True,
            ),
            ArgConsumer(
                ['--do-not-output-stderrs'],
                help='Do not output any stderrs',
                hook=SetConstValueHook('do_not_output_stderrs', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--mask-roots'],
                help='Mask source and build root paths in stderr',
                hook=SetConstValueHook('mask_roots', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--no-mask-roots'],
                help='Do not mask source and build root paths in stderr',
                hook=SetConstValueHook('mask_roots', False),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer('output_style'),
            ConfigConsumer('mask_roots'),
            ConfigConsumer('use_roman_numerals'),
        ]

    @classmethod
    def consumer(cls):
        return cls.additional_style_opts() + cls.common_style_opts()

    def postprocess2(self, params):
        if params.mask_roots is not None:
            return
        params.mask_roots = getattr(params, "use_distbuild", False)

        if config.is_developer_ya_version():
            params.use_roman_numerals = True


class ProfileOptions(Options):
    def __init__(self):
        self.profile_to_file = None
        self.stages_profile = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--profile'],
                help='Write profile info to file',
                hook=SetValueHook('profile_to_file'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--stages'],
                help='Write stages info to file',
                hook=SetValueHook('stages_profile'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]

    def postprocess(self):
        profiler.clear(self.profile_to_file)
        devtools.ya.core.stages_profiler.clear(self.stages_profile)


class CustomSourceRootOptions(Options):
    def __init__(self):
        self.custom_source_root = None

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['-S', '--source-root'],
            help='Custom source root (autodetected by default)',
            hook=SetValueHook('custom_source_root'),
            group=OUTPUT_CONTROL_GROUP,
            visible=HelpLevel.INTERNAL,
        )


class CustomBuildRootOptions(Options):
    def __init__(self):
        self.custom_build_directory = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-B', '--build-dir'],
                help='Custom build directory (autodetected by default)',
                hook=SetValueHook('custom_build_directory'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer("build_dir", hook=SetValueHook('custom_build_directory')),
        ]

    def postprocess(self):
        if self.custom_build_directory is not None:
            self.custom_build_directory = exts.path2.abspath(self.custom_build_directory, expand_user=True)


class CustomMiscBuildInfoDirOptions(Options):
    def __init__(self):
        self.misc_build_info_dir = None

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--misc-build-info-dir'],
            help='Directory for miscellaneous build files (build directory by default)',
            hook=SetValueHook('misc_build_info_dir'),
            group=GRAPH_GENERATION_GROUP,
            visible=HelpLevel.INTERNAL,
        )


class DryRunOptions(Options):
    def __init__(self):
        self.dry_run = False

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--dry-run'],
            help='Do not actually do anything',
            hook=SetConstValueHook('dry_run', True),
            group=BULLET_PROOF_OPT_GROUP,
        )


class PrintStatisticsOptions(Options):
    def __init__(self):
        self.print_statistics = False
        self.statistics_out_dir = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--stat'],
                help='Show build execution statistics',
                hook=SetConstValueHook('print_statistics', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--stat-dir'],
                help='Additional statistics output dir',
                hook=SetValueHook('statistics_out_dir'),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer('print_statistics'),
            ConfigConsumer('statistics_out_dir'),
        ]


class KeepTempsOptions(Options):
    def __init__(self):
        self.keep_temps = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--keep-temps'],
                help="Do not remove temporary build roots. Print test's working directory to the stderr (use --test-stderr to make sure it's printed at the test start)",
                hook=SetConstValueHook('keep_temps', True),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer('YA_KEEP_TEMPS', hook=SetValueHook('keep_temps', return_true_if_enabled)),
        ]

    def postprocess2(self, params):
        # Enable --keep-temps in ya-bin executed indirectly, for example, through test_tool.
        if params.keep_temps:
            os.environ['YA_KEEP_TEMPS'] = '1'


class HtmlDisplayOptions(Options):
    def __init__(self):
        self.html_display = None

    @staticmethod
    def consumer():
        help = 'Alternative output in html format'
        hook = SetValueHook('html_display')
        return [
            ArgConsumer(
                ['--html-display'],
                help=help,
                hook=hook,
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer(
                name='YA_HTML_DISPLAY',
                help=help,
                hook=hook,
            ),
        ]


class TeamcityOptions(Options):
    def __init__(self):
        self.teamcity = False

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--teamcity'],
            help='Generate additional info for teamcity',
            hook=SetConstValueHook('teamcity', const=True),
            group=PRINT_CONTROL_GROUP,
            visible=HelpLevel.EXPERT,
        )


class ProfilerOptions(Options):
    def __init__(self):
        self.profile_to = None

    @staticmethod
    def consumer():
        return ArgConsumer(
            ['--profile-to'],
            help='Run with cProfile',
            hook=SetValueHook('profile_to'),
            group=DEVELOPERS_OPT_GROUP,
            visible=HelpLevel.INTERNAL,
        )


class LogFileOptions(Options):
    def __init__(self):
        self.log_file = None

    @staticmethod
    def consumer():
        help = 'Append verbose log into specified file'
        hook = SetValueHook('log_file')

        return [
            ArgConsumer(
                ['--log-file'],
                help=help,
                hook=hook,
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            EnvConsumer(name='YA_LOG_FILE', help=help, hook=hook),
        ]


class EventLogFileOptions(Options):
    def __init__(self):
        self.evlog_file = None
        self.no_evlogs = False
        self.dump_platform_to_evlog = False
        self.dump_failed_node_info_to_evlog = False
        self.evlog_dump_node_stat = False
        self.compress_evlog = True

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--evlog-file'],
                help='Dump event log into specified file (file will be compressed using zstd if the file name ends with ".zst" or ".zstd")',
                hook=SetValueHook('evlog_file'),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                name='YA_EVLOG_FILE', help='Dump event log into specified file', hook=SetValueHook('evlog_file')
            ),
            ArgConsumer(
                ['--no-evlogs'],
                help='Disable standard evlogs in YA_CACHE_DIR',
                hook=SetConstValueHook('no_evlogs', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(name='YA_NO_EVLOGS', hook=SetConstValueHook('no_evlogs', True)),
            ArgConsumer(
                ['--evlog-dump-platform'],
                help='Add platform in event message',
                hook=SetConstValueHook('dump_platform_to_evlog', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                name='YA_EVLOG_DUMP_PLATFORM',
                hook=SetConstValueHook('dump_platform_to_evlog', True),
            ),
            ArgConsumer(
                ['--evlog-dump-failed-node-info'],
                visible=False,
                help='Put failed nodes info to evlog',
                hook=SetConstValueHook('dump_failed_node_info_to_evlog', True),
                group=DEVELOPERS_OPT_GROUP,
            ),
            EnvConsumer(
                name='YA_EVLOG_DUMP_FAILED_NODE_INFO',
                hook=SetConstValueHook('dump_failed_node_info_to_evlog', True),
            ),
            ArgConsumer(
                ['--evlog-node-stat'],
                help='Dump node execution statistics to evlog (distbuild-only)',
                hook=SetConstValueHook('evlog_dump_node_stat', True),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                name='YA_EVLOG_NODE_STAT',
                hook=SetConstValueHook('evlog_dump_node_stat', return_true_if_enabled),
            ),
            ArgConsumer(
                ['--no-compress-evlog'],
                help='Disable evlog compression',
                hook=SetConstValueHook('compress_evlog', False),
                group=PRINT_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(name='YA_NO_COMPRESS_EVLOG', hook=SetConstValueHook('compress_evlog', False)),
            ConfigConsumer('compress_evlog'),
        ]

    def postprocess2(self, params):
        if not self.no_evlogs and len(getattr(params, 'target_platforms', [])) > 1:
            self.dump_platform_to_evlog = True


class StdoutOptions(Options):
    def __init__(self):
        self.stdout = None


class TerminalProfileOptions(Options):
    def __init__(self):
        self.terminal_profile = {}

    @staticmethod
    def consumer():
        return ConfigConsumer('terminal_profile', help='Allows to remap markup colors (e.g bad = "light-red")')


class MiniYaOpts(Options):
    def __init__(self):
        self.thin_checkout = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--thin'],
                help='Checkout minimal skeleton',
                hook=SetConstValueHook('thin_checkout', True),
                group=CHECKOUT_ONLY_GROUP,
                visible=HelpLevel.EXPERT,
            ),
        ]


class CrossCompilationOptions(Options):
    class PlatformSetAppendHook(SetAppendHook):
        def _validate(self, lst):
            # XXX DEVTOOLS-5818
            pass

    class PlatformParamHook(BaseHook):
        def ensure_at_least_one_platform(self, opts):
            if not opts.target_platforms:
                opts.target_platforms.append(CrossCompilationOptions.make_platform('host_platform'))

    class PlatformsSetExtraConstParamHook(PlatformParamHook):
        def __init__(self, key, value):
            self.key = key
            self.value = value

        def __call__(self, to):
            self.ensure_at_least_one_platform(to)
            to.target_platforms[-1][self.key] = self.value

        @staticmethod
        def need_value():
            return False

    class PlatformsSetExtraParamHook(PlatformParamHook):
        def __init__(self, name, key):
            self.name = name
            self.key = key

        def __call__(self, to, value):
            self.ensure_at_least_one_platform(to)
            to.target_platforms[-1][self.key] = value

        @staticmethod
        def need_value():
            return True

    class PlatformsSetExtraAppendParamHook(PlatformParamHook):
        def __init__(self, name, key):
            self.name = name
            self.key = key

        def __call__(self, to, value):
            self.ensure_at_least_one_platform(to)
            if self.key not in to.target_platforms[-1]:
                to.target_platforms[-1][self.key] = []
            to.target_platforms[-1][self.key].append(value)

        @staticmethod
        def need_value():
            return True

    class PlatformsSetConstExtraExtendParamHook(PlatformParamHook):
        def __init__(self, name, key, data):
            self.name = name
            self.key = key
            self.data = data

        def __call__(self, to):
            self.ensure_at_least_one_platform(to)
            if self.key not in to.target_platforms[-1]:
                to.target_platforms[-1][self.key] = []
            to.target_platforms[-1][self.key].extend(self.data)

        @staticmethod
        def need_value():
            return False

    class PlatformsSetExtraDictParamHook(PlatformParamHook):
        def __init__(self, name, key, default_value):
            self.name = name
            self.key = key
            self.default_value = default_value

        def __call__(self, to, x):
            self.ensure_at_least_one_platform(to)
            dict_key, value = (x.split('=', 1) + [self.default_value])[:2]
            to.target_platforms[-1][self.key][dict_key] = value

        @staticmethod
        def need_value():
            return True

    def __init__(self, visible=None):
        super(CrossCompilationOptions, self).__init__(visible=visible)
        self.advanced = True
        self.c_compiler = None
        self.cxx_compiler = None
        self.host_platform = None
        self.host_platform_flags = {}
        self.host_build_type = 'release'
        self.target_platforms = []
        self.platform_schema_validation = False
        self.hide_arm64_host_warning = False

    @staticmethod
    def make_platform(platform_name):
        return {
            'platform_name': platform_name,
            'run_tests': None,
            'build_type': None,
            'flags': {},
            'c_compiler': None,
            'cxx_compiler': None,
        }

    @staticmethod
    @exts.func.lazy
    def generate_target_platforms_cxx():
        import yalibrary.platform_matcher as pm
        import yalibrary.tools

        platforms = []
        for tool in yalibrary.tools.iter_tools('c++'):
            host_str = pm.stringize_platform(tool['platform']['host'])
            platforms.append(host_str)
            target_str = pm.stringize_platform(tool['platform']['target'])
            platforms.append(target_str)
        return platforms

    def _validate_platform_list(self, data):
        if not self.platform_schema_validation:
            return data

        if not isinstance(data, (list, tuple)):
            raise ArgsValidatingException(
                "List of platforms must be list, not {}. "
                "Use [[target_platform]] for set target platform".format(type(data))
            )

        processed_data = []

        for item in data:
            processed_data.append(self._validate_platform(item))

        return processed_data

    def _validate_platform(self, data):
        # type: (dict) -> dict
        # TODO: Deep convert dict items to str
        data = {six.ensure_str(key): value for key, value in six.iteritems(data)}

        if not self.platform_schema_validation:
            return data

        valid_keys = {
            'platform_name',
            'c_compiler',
            'cxx_compiler',
            'flags',
            'run_tests',
            'build_type',
            'targets',
            'test_type_filters',
            'test_size_filters',
            'test_class_filters',
            'ignore_recurses',
        }

        wrong_keys = set(data.keys()) - valid_keys

        if wrong_keys:
            raise ArgsValidatingException(
                "Wrong keys: {}. Use something of this instead: {}".format(wrong_keys, valid_keys)
            )

        if not isinstance(data, dict):
            raise ArgsValidatingException("Platform info must be dict, not {}".format(type(data)))

        platform_name = data.get('platform_name', "UNSET")

        for str_attr in ('platform_name', 'build_type'):
            if data.get(str_attr) is None:
                continue

            if not isinstance(data[str_attr], six.string_types):
                raise ArgsValidatingException(
                    "In platform `{}`: {} must be string, not {}".format(platform_name, str_attr, type(data[str_attr]))
                )

        for path_attr in ('c_compiler', 'cxx_compiler'):
            if data.get(path_attr) is None:
                continue

            if not isinstance(data[path_attr], six.string_types):
                raise ArgsValidatingException(
                    "In platform `{}` {} must be path, not {}".format(platform_name, path_attr, type(data[path_attr]))
                )

        if not isinstance(data.get('flags', {}), dict):
            raise ArgsValidatingException(
                "In platform `{}` flags must be dict, not {}. Use [target_platform.flags] instead".format(
                    platform_name, data['flags']
                )
            )

        for bool_attr in ('run_tests', 'ignore_recurses'):
            if data.get(bool_attr) is None:
                continue

            if not isinstance(data[bool_attr], bool):
                raise ArgsValidatingException(
                    "In platform `{}` {} must be bool or unset, not {}".format(
                        platform_name, bool_attr, type(data[bool_attr])
                    )
                )

        for list_of_str_items in ('targets', 'test_type_filters', 'test_size_filters', 'test_class_filters'):
            if data.get(list_of_str_items) is None:
                continue

            if not isinstance(data[list_of_str_items], (list, tuple)):
                raise ArgsValidatingException(
                    "In platform `{}` {} must be list, not {}".format(
                        platform_name, list_of_str_items, type(data[list_of_str_items])
                    )
                )

            for i, item in enumerate(data[list_of_str_items]):
                if not isinstance(item, six.string_types):
                    raise ArgsValidatingException(
                        "In platform `{}` {}[{}] must be string, not {}".format(
                            platform_name, list_of_str_items, i, type(item)
                        )
                    )

        return data

    def consumer(self):
        consumers = [
            BuildTypeConsumer(
                ['--host-build-type'],
                option='host_build_type',
                short_help='Host platform build type',
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--host-platform'],
                help='Host platform',
                hook=SetValueHook('host_platform'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ConfigConsumer('host_platform'),
            ArgConsumer(
                ['--host-platform-flag'],
                help='Host platform flag',
                hook=DictPutHook('host_platform_flags', default_value='yes'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ConfigConsumer('host_platform_flags', hook=DictUpdateHook('host_platform_flags')),
            ArgConsumer(
                ['--c-compiler'],
                help='Specifies path to the custom compiler for the host and target platforms',
                hook=SetValueHook('c_compiler'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--cxx-compiler'],
                help='Specifies path to the custom compiler for the host and target platforms',
                hook=SetValueHook('cxx_compiler'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--target-platform'],
                help='Target platform',
                hook=CrossCompilationOptions.PlatformSetAppendHook(
                    'target_platforms',
                    values=CrossCompilationOptions.generate_target_platforms_cxx,
                    transform=CrossCompilationOptions.make_platform,
                ),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--target-platform-build-type'],
                help='Set build type for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraParamHook('target_platform_build_type', 'build_type'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-release'],
                help='Set release build type for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraConstParamHook('build_type', 'release'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-debug'],
                help='Set debug build type for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraConstParamHook('build_type', 'debug'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-tests'],
                help='Run tests for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraConstParamHook('run_tests', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-test-size'],
                help='Run tests only with given size for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraAppendParamHook(
                    'target_platform_test_size', 'test_size_filters'
                ),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-test-type'],
                help='Run tests only with given type for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraAppendParamHook(
                    'target_platform_test_type', 'test_type_filters'
                ),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-regular-tests'],
                help='Run only regular test types for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetConstExtraExtendParamHook(
                    'target_platform_regular_tests',
                    'test_class_filters',
                    [devtools.ya.test.const.SuiteClassType.REGULAR],
                ),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-flag'],
                help='Set build flag for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraDictParamHook('target_platform_flag', 'flags', 'yes'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--target-platform-c-compiler'],
                help='Specifies path to the custom compiler for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraParamHook('target_platform_compiler', 'c_compiler'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-cxx-compiler'],
                help='Specifies path to the custom compiler for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraParamHook('target_platform_compiler', 'cxx_compiler'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-target'],
                help='Source root relative build targets for the last target platform',
                hook=CrossCompilationOptions.PlatformsSetExtraAppendParamHook('target_platform_target', 'targets'),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ArgConsumer(
                ['--target-platform-ignore-recurses'],
                help='Do not build by RECURSES',
                hook=CrossCompilationOptions.PlatformsSetExtraConstParamHook('ignore_recurses', True),
                group=PLATFORM_CONFIGURATION_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            ConfigConsumer(
                'target_platform',
                hook=ExtendHook('target_platforms', transform=self._validate_platform_list, values=None),
            ),
            ConfigConsumer('platform_schema_validation'),
            ArgConsumer(
                ['--disable-platform-schema-validation'],
                help='Do not validate target-platforms',
                hook=SetConstValueHook('platform_schema_validation', False),
                group=FEATURES_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            ArgConsumer(
                ['--enable-platform-schema-validation'],
                help='Do not validate target-platforms',
                hook=SetConstValueHook('platform_schema_validation', True),
                group=FEATURES_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            ArgConsumer(
                ['--hide-arm64-host-warning'],
                help='Hide MacOS arm64 host warning',
                hook=SetConstValueHook('hide_arm64_host_warning', True),
                group=ADVANCED_OPT_GROUP,
                visible=is_darwin_rosetta(),
            ),
            EnvConsumer('YA_TOOL_HIDE_ARM64_HOST_WARNING', hook=SetConstValueHook('hide_arm64_host_warning', True)),
            ConfigConsumer('hide_arm64_host_warning'),
        ]

        return consumers

    def postprocess(self):
        self._validate_platform_list(self.target_platforms)

        # for pl in self.target_platforms:
        #     if pl.get('run_tests'):
        #         pl['flags']['TESTS_REQUESTED'] = 'yes'


class CommonUploadOptions(Options):
    def __init__(self, visible=HelpLevel.EXPERT):
        super(CommonUploadOptions, self).__init__(visible=visible)
        self.ttl = 14

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--ttl'],
                help='Resource TTL in days (pass \'inf\' - to mark resource not removable)',
                hook=SetValueHook(name='ttl', transform=ttl_transform),
                group=COMMON_UPLOAD_OPT_GROUP,
            )
        ]


def ttl_transform(val):
    try:
        return int(val)
    except ValueError:
        if val.lower() == "inf":
            return yalibrary.upload.consts.TTL_INF
    raise ValueError("Invalid value for TTL: {}".format(val))


class AuthOptions(Options):
    def __init__(self, extra_env_vars=None, ssh_key_option_name="--ssh-key", visible=None):
        super(AuthOptions, self).__init__(visible=visible)
        self._extra_env_vars = extra_env_vars
        self.oauth_token = None
        self.oauth_token_path = None
        self.docker_config_path = None
        self.username = None
        self.ssh_keys = []
        self.ssh_key_option_name = ssh_key_option_name

        # Fall-back option, remove
        self.oauth_exchange_ssh_keys = True

        self.store_oauth_token = True

    def consumer(self):
        res = [
            EnvConsumer(
                'YA_TOKEN',
                hook=SetValueHook('oauth_token'),
            ),
            ArgConsumer(
                [self.ssh_key_option_name],
                hook=SetAppendHook('ssh_keys'),
                group=AUTH_OPT_GROUP,
                help='Path to private ssh key to exchange for OAuth token',
                visible=HelpLevel.ADVANCED,
            ),
            ConfigConsumer('oauth_token'),
            ConfigConsumer('oauth_token_path'),
            ConfigConsumer('oauth_exchange_ssh_keys'),
            EnvConsumer(
                'YA_OAUTH_EXCHANGE_SSH_KEYS',
                hook=SetValueHook('oauth_exchange_ssh_keys', return_true_if_enabled),
            ),
            ArgConsumer(
                ['--docker-config-path'],
                help='Path to docker config file. Use ~/.docker/config.json by default',
                hook=SetValueHook('docker_config_path'),
                group=AUTH_OPT_GROUP,
            ),
            ConfigConsumer('docker_config_path'),
            EnvConsumer('YA_DOCKER_CONFIG_PATH', hook=SetValueHook('docker_config_path')),
            EnvConsumer(
                'YA_STORE_TOKEN',
                hook=SetValueHook('store_oauth_token', return_true_if_enabled),
            ),
        ]
        if self._extra_env_vars:
            res.extend(EnvConsumer(var, hook=SetValueHook('oauth_token')) for var in self._extra_env_vars)
        return res

    def postprocess(self):
        if self.username is None:
            self.username = config.get_user()

        if self.oauth_token:
            # Ignore token path
            self.oauth_token_path = None
        else:
            self.oauth_token_path = self.oauth_token_path or cc.get_ya_token_path()
            if self.oauth_token_path:
                token = self._read_token_file(self.oauth_token_path)
                if token:
                    self.oauth_token = token
                else:
                    self.oauth_token_path = None

        self._find_docker_config()

    def _find_docker_config(self):
        if self.docker_config_path:
            return
        docker_config_paths = cc.get_docker_config_paths()
        for docker_config_path in docker_config_paths:
            if os.path.exists(docker_config_path):
                self.docker_config_path = docker_config_path
                return

    # TODO: Use devtools/libs/ya_token here
    @staticmethod
    def _read_token_file(path):
        try:
            with open(path) as afile:
                token = afile.read().strip()
        except Exception as e:
            logger.debug('Could not read file at %s: %s', path, e)
            return

        if not token:
            logger.debug('Attempted to read a token from %s, but the file is empty', path)
            return

        return token

    def postprocess2(self, params):
        if not cc.is_self_contained_runner3(params):
            params.oauth_token_path = None


class DumpDebugCommonOptions(Options):
    def __init__(self):
        self.dump_debug_path = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--dump-debug-to'],
                help="Path to save dump debugs",
                hook=SetValueHook('dump_debug_path'),
                group=DEVELOPERS_OPT_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            EnvConsumer(
                name='YA_DUMP_DEBUG_TO',
                help="Path to save dump debugs",
                hook=SetValueHook('dump_debug_path'),
            ),
        ]


class DumpDebugOptions(Options):
    def __init__(self):
        self.dump_debug_enabled = False

    @staticmethod
    def consumer():
        dump_debug_help = "Enable storing data for debug bundle"
        dump_debug_hook = SetConstValueHook('dump_debug_enabled', True)

        return [
            ArgConsumer(
                ['--dump-debug'],
                help="Enable dump debug",
                hook=dump_debug_hook,
                group=FEATURES_GROUP,
                visible=HelpLevel.INTERNAL,
                deprecated=True,
            ),
            ArgConsumer(
                ['--no-dump-debug'],
                help="Disable dump debug",
                hook=SetConstValueHook('dump_debug_enabled', False),
                group=FEATURES_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            EnvConsumer(
                name='YA_DUMP_DEBUG',
                help=dump_debug_help,
                hook=SetValueHook("dump_debug_enabled", return_true_if_enabled),
            ),
            ConfigConsumer("dump_debug_enabled"),
        ]


class YaBin3Options(Options):
    def __init__(self):
        self.ya_bin3_required = None

    @staticmethod
    def consumer():
        return [
            ConfigConsumer("enable_ya_bin3", hook=SetValueHook("ya_bin3_required", transform=bool)),
        ]
