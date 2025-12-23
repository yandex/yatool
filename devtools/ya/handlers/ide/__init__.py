import os.path

import devtools.ya.core.yarg as yarg
import devtools.ya.core.config
import devtools.ya.core.common_opts

import devtools.ya.build.build_opts as build_opts
import devtools.ya.build.compilation_database as bcd

import devtools.ya.ide.ide_common
import devtools.ya.ide.clion2016
import devtools.ya.ide.idea
import devtools.ya.ide.goland
import devtools.ya.ide.pycharm
import devtools.ya.ide.venv
import devtools.ya.ide.vscode_all
import devtools.ya.ide.vscode_clangd
import devtools.ya.ide.vscode_go
import devtools.ya.ide.vscode_py
import devtools.ya.ide.vscode_ts
import devtools.ya.ide.vscode.opts
import devtools.ya.ide.gradle

import yalibrary.platform_matcher as pm

import devtools.ya.app
import app_config

if app_config.in_house:
    import devtools.ya.ide.fsnotifier

from devtools.ya.core.yarg.help_level import HelpLevel


class TidyOptions(yarg.Options):
    def __init__(self):
        self.setup_tidy = False

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ['--setup-tidy'],
                help="Setup default arcadia's clang-tidy config in a project",
                hook=yarg.SetConstValueHook('setup_tidy', True),
                group=yarg.BULLET_PROOF_OPT_GROUP,
            ),
            yarg.ConfigConsumer('setup_tidy'),
        ]


class CLionOptions(yarg.Options):
    CLION_OPT_GROUP = yarg.Group('CLion project options', 0)

    def __init__(self):
        self.filters = []
        self.lite_mode = False
        self.remote_toolchain = None
        self.remote_deploy_config = None
        self.remote_repo_path = None
        self.remote_build_path = None
        self.remote_deploy_host = None
        self.use_sync_server = False
        self.content_root = None
        self.strip_non_final_targets = False
        self.full_targets = False
        self.add_py_targets = False

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ['--filter', '-f'],
                help='Only consider filtered content',
                hook=yarg.SetAppendHook('filters'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--mini', '-m'],
                help='Lite mode for solution (fast open, without build)',
                hook=yarg.SetConstValueHook('lite_mode', True),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--remote-toolchain'],
                help='Generate configurations for remote toolchain with this name',
                hook=yarg.SetValueHook('remote_toolchain'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--remote-deploy-config'],
                help='Name of the remote server configuration tied to the remote toolchain',
                hook=yarg.SetValueHook('remote_deploy_config'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--remote-repo-path'],
                help='Path to the arc repository at the remote host',
                hook=yarg.SetValueHook('remote_repo_path'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--remote-build-path'],
                help='Path to the directory for CMake output at the remote host',
                hook=yarg.SetValueHook('remote_build_path'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--remote-host'],
                help='Hostname associated with remote server configuration',
                hook=yarg.SetValueHook('remote_deploy_host'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--use-sync-server'],
                help='Deploy local files via sync server instead of file watchers',
                hook=yarg.SetConstValueHook('use_sync_server', True),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--project-root', '-r'],
                help='Root directory for a CLion project',
                hook=yarg.SetValueHook('content_root'),
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--strip-non-final-targets'],
                hook=yarg.SetConstValueHook('strip_non_final_targets', True),
                help='Do not create target for non-final nodes',
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--full-targets'],
                hook=yarg.SetConstValueHook('full_targets', True),
                help='Old Mode: Enable full targets graph generation for project.',
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--add-py3-targets'],
                hook=yarg.SetConstValueHook('add_py_targets', True),
                help='Add Python 3 targets to project',
                group=CLionOptions.CLION_OPT_GROUP,
            ),
            yarg.ConfigConsumer('filters'),
            yarg.ConfigConsumer('lite_mode'),
            yarg.ConfigConsumer('remote_toolchain'),
            yarg.ConfigConsumer('remote_deploy_config'),
            yarg.ConfigConsumer('remote_repo_path'),
            yarg.ConfigConsumer('remote_build_path'),
            yarg.ConfigConsumer('remote_deploy_host'),
            yarg.ConfigConsumer('use_sync_server'),
            yarg.ConfigConsumer('content_root'),
            yarg.ConfigConsumer('strip_non_executable_target'),
            yarg.ConfigConsumer('full_targets'),
            yarg.ConfigConsumer('add_py_targets'),
        ]

    def postprocess2(self, params):
        if ' ' in params.project_title:
            raise yarg.ArgsValidatingException('Clion project title should not contain space symbol')
        if params.add_py_targets and params.full_targets:
            raise yarg.ArgsValidatingException('--add-py-targets must be used without --full-targets')


class IdeaOptions(yarg.Options):
    IDEA_OPT_GROUP = yarg.Group('Idea project options', 0)
    IDE_PLUGIN_INTEGRATION_GROUP = yarg.Group('Integration with IDE plugin', 1)

    def __init__(self):
        self.idea_project_root = None
        self.local = False
        self.group_modules = None
        self.dry_run = False
        self.ymake_bin = None
        self.iml_in_project_root = False
        self.iml_keep_relative_paths = False
        self.idea_files_root = None
        self.omit_test_data = False
        self.with_content_root_modules = False
        self.external_content_root_modules = []
        self.generate_tests_run = False
        self.generate_tests_for_deps = False
        self.separate_tests_modules = False
        self.auto_exclude_symlinks = False
        self.exclude_dirs = []
        self.with_common_jvm_args_in_junit_template = False
        self.with_long_library_names = False
        self.copy_shared_index_config = False
        self.idea_jdk_version = None
        self.regenerate_with_project_update = False
        self.project_update_targets = []
        self.project_update_kind = None

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ['-r', '--project-root'],
                help='IntelliJ IDEA project root path',
                hook=yarg.SetValueHook('idea_project_root'),
                group=IdeaOptions.IDEA_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['-P', '--project-output'],
                help='IntelliJ IDEA project root path. Please, use instead of -r',
                hook=yarg.SetValueHook('idea_project_root'),
                group=IdeaOptions.IDEA_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['-l', '--local'],
                help='Only recurse reachable projects are idea modules',
                hook=yarg.SetConstValueHook('local', True),
                group=IdeaOptions.IDEA_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--group-modules'],
                help='Group idea modules according to paths',
                hook=yarg.SetValueHook('group_modules', values=('tree', 'flat')),
                group=IdeaOptions.IDEA_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['-n', '--dry-run'],
                help='Emulate create project, but do nothing',
                hook=yarg.SetConstValueHook('dry_run', True),
                group=IdeaOptions.IDEA_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--ymake-bin'],
                help='Path to ymake binary',
                hook=yarg.SetValueHook('ymake_bin'),
                visible=False,
            ),
            yarg.ArgConsumer(
                ['--iml-in-project-root'],
                help='Store ".iml" files in project root tree(stores in source root tree by default)',
                hook=yarg.SetConstValueHook('iml_in_project_root', True),
            ),
            yarg.ArgConsumer(
                ['--iml-keep-relative-paths'],
                help='Keep relative paths in ".iml" files (works with --iml-in-project-root)',
                hook=yarg.SetConstValueHook('iml_keep_relative_paths', True),
            ),
            yarg.ArgConsumer(
                ['--idea-files-root'],
                help='Root for .ipr and .iws files',
                hook=yarg.SetValueHook('idea_files_root'),
            ),
            yarg.ArgConsumer(
                ['--omit-test-data'],
                help='Do not export test_data',
                hook=yarg.SetConstValueHook('omit_test_data', True),
            ),
            yarg.ArgConsumer(
                ['--with-content-root-modules'],
                help='Generate content root modules',
                hook=yarg.SetConstValueHook('with_content_root_modules', True),
            ),
            yarg.ArgConsumer(
                ['--external-content-root-module'],
                help='Add external content root modules',
                hook=yarg.SetAppendHook('external_content_root_modules'),
            ),
            yarg.ArgConsumer(
                ['--generate-junit-run-configurations'],
                help='Generate run configuration for junit tests',
                hook=yarg.SetConstValueHook('generate_tests_run', True),
            ),
            yarg.ArgConsumer(
                ['--generate-tests-for-dependencies'],
                help='Generate tests for PEERDIR dependencies',
                hook=yarg.SetConstValueHook('generate_tests_for_deps', True),
            ),
            yarg.ArgConsumer(
                ['--separate-tests-modules'],
                help='Do not merge tests modules with their own libraries',
                hook=yarg.SetConstValueHook('separate_tests_modules', True),
            ),
            yarg.ArgConsumer(
                ['--auto-exclude-symlinks'],
                help='Add all symlink-dirs in modules to exclude dirs',
                hook=yarg.SetConstValueHook('auto_exclude_symlinks', True),
            ),
            yarg.ArgConsumer(
                ['--exclude-dirs'],
                help='Exclude dirs with specific names from all modules',
                hook=yarg.SetAppendHook('exclude_dirs'),
            ),
            yarg.ArgConsumer(
                ['--with-common-jvm-args-in-junit-template'],
                help='Add common JVM_ARGS flags to default junit template',
                hook=yarg.SetConstValueHook('with_common_jvm_args_in_junit_template', True),
            ),
            yarg.ArgConsumer(
                ['--with-long-library-names'],
                help='Generate long library names',
                hook=yarg.SetConstValueHook('with_long_library_names', True),
            ),
            yarg.ArgConsumer(
                ['--copy-shared-index-config'],
                help='Copy project config for Shared Indexes if exist',
                hook=yarg.SetConstValueHook('copy_shared_index_config', True),
            ),
            yarg.ArgConsumer(
                ['--idea-jdk-version'],
                help='Project JDK version',
                hook=yarg.SetValueHook('idea_jdk_version'),
            ),
            yarg.ArgConsumer(
                ['-U', '--regenerate-with-project-update'],
                help='Run `ya project update` upon regeneration from Idea',
                group=IdeaOptions.IDE_PLUGIN_INTEGRATION_GROUP,
                hook=yarg.SetConstValueHook('regenerate_with_project_update', True),
            ),
            yarg.ArgConsumer(
                ['--project-update-targets'],
                help='Run `ya project update` for this dirs upon regeneration from Idea',
                hook=yarg.SetAppendHook('project_update_targets'),
                group=IdeaOptions.IDE_PLUGIN_INTEGRATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            yarg.ArgConsumer(
                ['--project-update-kind'],
                help='Type of a project to use in `ya project update` upon regernation from Idea',
                hook=yarg.SetValueHook('project_update_kind'),
                group=IdeaOptions.IDE_PLUGIN_INTEGRATION_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            yarg.ConfigConsumer('idea_project_root'),
            yarg.ConfigConsumer('local'),
            yarg.ConfigConsumer('group_modules'),
            yarg.ConfigConsumer('dry_run'),
            yarg.ConfigConsumer('iml_in_project_root'),
            yarg.ConfigConsumer('iml_keep_relative_paths'),
            yarg.ConfigConsumer('idea_files_root'),
            yarg.ConfigConsumer('omit_test_data'),
            yarg.ConfigConsumer('with_content_root_modules'),
            yarg.ConfigConsumer('external_content_root_modules'),
            yarg.ConfigConsumer('generate_tests_run'),
            yarg.ConfigConsumer('generate_tests_for_deps'),
            yarg.ConfigConsumer('separate_tests_modules'),
            yarg.ConfigConsumer('auto_exclude_symlinks'),
            yarg.ConfigConsumer('exclude_dirs'),
            yarg.ConfigConsumer('with_common_jvm_args_in_junit_template'),
            yarg.ConfigConsumer('with_long_library_names'),
            yarg.ConfigConsumer('copy_shared_index_config'),
            yarg.ConfigConsumer('idea_jdk_version'),
            yarg.ConfigConsumer('regenarate_with_project_update'),
            yarg.ConfigConsumer('project_update_targets'),
            yarg.ConfigConsumer('project_update_kind'),
        ]

    def postprocess(self):
        if self.idea_project_root is None:
            raise yarg.ArgsValidatingException('Idea project root(-r, --project-root) must be specified.')

        self.idea_project_root = os.path.abspath(self.idea_project_root)

        if self.iml_keep_relative_paths and not self.iml_in_project_root:
            raise yarg.ArgsValidatingException('--iml-keep-relative-paths can be used only with --iml-in-project-root')

        for p in self.exclude_dirs:
            if os.path.isabs(p):
                raise yarg.ArgsValidatingException('Absolute paths are not allowed in --exclude-dirs')


class GradleOptions(yarg.Options):
    YGRADLE_OPT_GROUP = yarg.Group('IDE Gradle project options', 0)

    OPT_GRADLE_NAME = '--gradle-name'
    OPT_SETTINGS_ROOT = '--settings-root'
    OPT_DISABLE_ERRORPRONE = '--disable-errorprone'
    OPT_DISABLE_TEST_ERRORPRONE = '--disable-test-errorprone'
    OPT_DISABLE_LOMBOK_PLUGIN = '--disable-lombok-plugin'
    OPT_DISABLE_GENERATED_SYMLINKS = '--disable-generated-symlinks'  # IGNORED IN CODE, only for backward compatibility
    OPT_FORCE_JDK_VERSION = '--force-jdk-version'
    OPT_GRADLE_JDK_VERSION = '--gradle-jdk-version'
    OPT_REMOVE = '--remove'
    OPT_EXCLUDE = '--exclude'
    OPT_GRADLE_DAEMON_JVMARGS = '--gradle-daemon-jvmargs'
    OPT_KOTLIN_DAEMON_JVMARGS = '--kotlin-daemon-jvmargs'
    OPT_SETTINGS_ROOT_AS_HASH_BASE = '--settings-root-as-hash-base'  # IGNORED IN CODE, only for backward compatibility
    OPT_JDK11_COMPATIBILITY_MODE = '--jdk11-compatibility-mode'

    # Advanced options
    ADVOPT_NO_COLLECT_CONTRIBS = '--no-collect-contribs'
    ADVOPT_NO_BUILD_FOREIGN = '--no-build-foreign'
    ADVOPT_REEXPORT = '--reexport'

    # Expert options
    EXPOPT_YEXPORT_BIN = '--yexport-bin'
    EXPOPT_YEXPORT_TOML = '--yexport-toml'
    EXPOPT_DUMP_YMAKE_STDERR = '--dump-ymake-stderr'
    EXPOPT_YEXPORT_DEBUG_MODE = '--yexport-debug-mode'
    EXPOPT_EXCLUSIVE_LOCK_BUILD = '--exclusive-lock-build'

    AVAILABLE_JDK_VERSIONS = ('11', '17', '21', '22', '23', '24', '25')
    # Gradle >= 9 require JDK17 or above
    GRADLE_JDK_VERSIONS = list(filter(lambda v: int(v) >= 17, AVAILABLE_JDK_VERSIONS))

    def __init__(self):
        self.gradle_name: str = None
        self.settings_root: str = None
        self.disable_errorprone: bool = False
        self.disable_test_errorprone: bool = False
        self.disable_lombok_plugin: bool = False
        self.disable_generated_symlinks: bool = False  # IGNORED IN CODE, only for backward compatibility
        self.force_jdk_version: str = None
        self.gradle_jdk_version: str = None
        self.remove: bool = False
        self.exclude: list[str] = []
        self.gradle_daemon_jvmargs: str = None
        self.kotlin_daemon_jvmargs: str = None
        self.settings_root_as_hash_base: bool = True  # IGNORED IN CODE, only for backward compatibility
        self.jdk11_compatibility_mode = False

        self.collect_contribs: bool = True
        self.build_foreign: bool = True
        self.reexport: bool = False

        self.yexport_bin: str = None
        self.yexport_toml: list[str] = []
        self.dump_ymake_stderr: str = None
        self.yexport_debug_mode: str = None
        self.exclusive_lock_build: bool = False

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                [GradleOptions.OPT_GRADLE_NAME],
                help='Set project name manually',
                hook=yarg.SetValueHook('gradle_name'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_SETTINGS_ROOT],
                help='Directory in Arcadia to place Gradle project settings (by default, if one target use target dir, else current dir or first target dir (if current dir not in arcadia))',
                hook=yarg.SetValueHook('settings_root'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_DISABLE_ERRORPRONE],
                help='Disable errorprone in Gradle project',
                hook=yarg.SetConstValueHook('disable_errorprone', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_DISABLE_TEST_ERRORPRONE],
                help='Disable errorprone only in tests in Gradle project',
                hook=yarg.SetConstValueHook('disable_test_errorprone', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_DISABLE_LOMBOK_PLUGIN],
                help='Disable lombok plugin in Gradle project',
                hook=yarg.SetConstValueHook('disable_lombok_plugin', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(  # IGNORED IN CODE, only for backward compatibility
                [GradleOptions.OPT_DISABLE_GENERATED_SYMLINKS],
                help='Disable make symlinks to generated sources (ENABLED BY DEFAULT - ONLY FOR BACKWARD COMPATIBILITY)',
                hook=yarg.SetConstValueHook('disable_generated_symlinks', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_FORCE_JDK_VERSION],
                help=f"Force JDK version for build/test in exported project, one of {', '.join(GradleOptions.AVAILABLE_JDK_VERSIONS)}",
                hook=yarg.SetValueHook('force_jdk_version'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_GRADLE_JDK_VERSION],
                help=f"Force JDK version for Gradle only in exported project, one of {', '.join(GradleOptions.GRADLE_JDK_VERSIONS)}",
                hook=yarg.SetValueHook('gradle_jdk_version'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_REMOVE],
                help='Remove gradle project files and all symlinks',
                hook=yarg.SetConstValueHook('remove', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_EXCLUDE],
                help='Exclude module and submodules from gradle project',
                hook=yarg.SetAppendHook('exclude'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_GRADLE_DAEMON_JVMARGS],
                help='Set org.gradle.jvmargs value for gradle.properties',
                hook=yarg.SetValueHook('gradle_daemon_jvmargs'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_KOTLIN_DAEMON_JVMARGS],
                help='Set kotlin.daemon.jvmargs value for gradle.properties',
                hook=yarg.SetValueHook('kotlin_daemon_jvmargs'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_SETTINGS_ROOT_AS_HASH_BASE],
                help='Use settings root as base for hashed export directory name (ignore export targets for hashed directory name) (ENABLED BY DEFAULT - ONLY FOR BACKWARD COMPATIBILITY)',
                hook=yarg.SetConstValueHook('settings_root_as_hash_base', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.OPT_JDK11_COMPATIBILITY_MODE],
                help='JDK11 compatibility mode',
                hook=yarg.SetConstValueHook('jdk11_compatibility_mode', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                [GradleOptions.ADVOPT_NO_COLLECT_CONTRIBS],
                help='Export without collect contribs from Arcadia to jar files',
                hook=yarg.SetConstValueHook('collect_contribs', False),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            yarg.ArgConsumer(
                [GradleOptions.ADVOPT_NO_BUILD_FOREIGN],
                help='Export without build foreign targets',
                hook=yarg.SetConstValueHook('build_foreign', False),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            yarg.ArgConsumer(
                [GradleOptions.ADVOPT_REEXPORT],
                help='Do remove before export',
                hook=yarg.SetConstValueHook('reexport', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.ADVANCED,
            ),
            yarg.ArgConsumer(
                [GradleOptions.EXPOPT_YEXPORT_BIN],
                help='Full path to yexport binary',
                hook=yarg.SetValueHook('yexport_bin'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            yarg.ArgConsumer(
                [GradleOptions.EXPOPT_YEXPORT_TOML],
                help='Global key=value for put to yexport.toml',
                hook=yarg.SetAppendHook('yexport_toml'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            yarg.ArgConsumer(
                [GradleOptions.EXPOPT_DUMP_YMAKE_STDERR],
                help='Dump stderr of YMake call to file (or to console if set to "log")',
                hook=yarg.SetValueHook('dump_ymake_stderr'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            yarg.ArgConsumer(
                [GradleOptions.EXPOPT_YEXPORT_DEBUG_MODE],
                help='Debug mode for yexport',
                hook=yarg.SetValueHook('yexport_debug_mode'),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
            yarg.ArgConsumer(
                [GradleOptions.EXPOPT_EXCLUSIVE_LOCK_BUILD],
                help='Exclusive lock during prebuild depends (recommended for few parallel ya ide gradle)',
                hook=yarg.SetConstValueHook('exclusive_lock_build', True),
                group=GradleOptions.YGRADLE_OPT_GROUP,
                visible=HelpLevel.EXPERT,
            ),
        ]

    def postprocess(self):
        if self.yexport_bin is not None and not os.path.exists(self.yexport_bin):
            raise yarg.ArgsValidatingException(
                f"Not found yexport binary {self.yexport_bin} in {GradleOptions.EXPOPT_YEXPORT_BIN}"
            )
        if self.gradle_name and self.remove:
            raise yarg.ArgsValidatingException(
                f"{GradleOptions.OPT_GRADLE_NAME} not applicable with {GradleOptions.OPT_REMOVE}"
            )
        if self.force_jdk_version is not None:
            if self.force_jdk_version not in GradleOptions.AVAILABLE_JDK_VERSIONS:
                raise yarg.ArgsValidatingException(
                    f"Invalid JDK version {self.force_jdk_version} in {GradleOptions.OPT_FORCE_JDK_VERSION}, must be one of {', '.join(GradleOptions.AVAILABLE_JDK_VERSIONS)}."
                )
        if self.gradle_jdk_version is not None:
            if self.gradle_jdk_version not in GradleOptions.GRADLE_JDK_VERSIONS:
                raise yarg.ArgsValidatingException(
                    f"Invalid JDK version {self.gradle_jdk_version} in {GradleOptions.OPT_GRADLE_JDK_VERSION}, must be one of {', '.join(GradleOptions.GRADLE_JDK_VERSIONS)}."
                )


class PycharmOptions(yarg.Options):
    PYCHARM_OPT_GROUP = yarg.Group('Pycharm project options', 0)
    PYTHON_WRAPPER_NAME = 'pycharm_python_wrapper'

    def __init__(self):
        self.only_generate_wrapper = False
        self.wrapper_name = PycharmOptions.PYTHON_WRAPPER_NAME
        self.list_ide = False
        self.ide_version = None
        self.do_codegen = True

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ['--only-generate-wrapper'],
                help="Don not generate Pycharm project, only wrappers",
                hook=yarg.SetConstValueHook('only_generate_wrapper', True),
                group=PycharmOptions.PYCHARM_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--wrapper-name'],
                help='Name of generated python wrapper. Use `python` for manual adding wrapper as Python SDK.',
                hook=yarg.SetValueHook('wrapper_name'),
                group=PycharmOptions.PYCHARM_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--list-ide'],
                help='List available JB IDE for patching SDK list.',
                hook=yarg.SetConstValueHook('list_ide', True),
                group=PycharmOptions.PYCHARM_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--ide-version'],
                help='Change IDE version for patching SDK list. Available IDE: {}'.format(
                    ", ".join(devtools.ya.ide.pycharm.find_available_ide())
                ),
                hook=yarg.SetValueHook('ide_version'),
                group=PycharmOptions.PYCHARM_OPT_GROUP,
            ),
            yarg.ArgConsumer(
                ['--no-codegen'],
                help='Disable codegen',
                hook=yarg.SetConstValueHook('do_codegen', False),
            ),
        ]

    def postprocess(self):
        if PycharmOptions.PYTHON_WRAPPER_NAME != self.wrapper_name and not self.only_generate_wrapper:
            raise yarg.ArgsValidatingException("Custom wrapper name can be used with option --only-generate-wrapper")


def get_description(text, ref_name):
    if app_config.in_house:
        ref = {
            "multi": "https://docs.yandex-team.ru/ya-make/usage/ya_ide/vscode#ya-ide-vscode",
            "typescript": "https://docs.yandex-team.ru/ya-make/usage/ya_ide/vscode#typescript",
        }[ref_name]
        return "{}\nDocs: [[c:dark-cyan]]{}[[rst]]".format(text, ref)
    else:
        return text


class IdeYaHandler(yarg.CompositeHandler):
    description = 'Generate project for IDE'

    def __init__(self):
        yarg.CompositeHandler.__init__(self, description=self.description)
        self['clion'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.clion2016.do_clion),
            description='[[imp]]ya ide clion[[rst]] is deprecated, please use clangd-based tooling instead',
            opts=devtools.ya.ide.ide_common.ide_via_ya_make_opts()
            + [
                CLionOptions(),
                TidyOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
        )

        self['idea'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.idea.do_idea),
            description='Generate stub for IntelliJ IDEA',
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                devtools.ya.ide.ide_common.IdeYaMakeOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                IdeaOptions(),
                devtools.ya.core.common_opts.OutputStyleOptions(),
                devtools.ya.core.common_opts.CrossCompilationOptions(),
                devtools.ya.core.common_opts.PrintStatisticsOptions(),
                build_opts.ContinueOnFailOptions(),
                build_opts.YMakeDebugOptions(),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.DistCacheOptions(),
                build_opts.FlagsOptions(),
                build_opts.IgnoreRecursesOptions(),
                build_opts.ContentUidsOptions(),
                build_opts.ExecutorOptions(),
                build_opts.CustomFetcherOptions(),
                build_opts.SandboxAuthOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
            unknown_args_as_free=True,
        )
        self['gradle'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.gradle.do_gradle),
            description='Generate gradle for project with yexport',
            opts=devtools.ya.build.build_opts.ya_make_options(build_type='release', free_build_targets=True)
            + [
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                GradleOptions(),
            ],
            unknown_args_as_free=True,
        )
        self['goland'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.goland.do_goland),
            description='Generate stub for Goland',
            opts=devtools.ya.ide.ide_common.ide_via_ya_make_opts()
            + [
                devtools.ya.ide.goland.GolandOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
        )
        self['pycharm'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.pycharm.do_pycharm),
            description='Generate PyCharm project.',
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                PycharmOptions(),
                devtools.ya.ide.ide_common.IdeYaMakeOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                build_opts.DistCacheOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
            visible=(pm.my_platform() != 'win32'),
        )
        self['vscode-clangd'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.vscode_clangd.gen_vscode_workspace),
            description="[[bad]]ya ide vscode-clangd[[rst]] is deprecated, please use [[good]]ya ide vscode --cpp[[rst]] instead",
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                devtools.ya.ide.vscode_clangd.VSCodeClangdOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                bcd.CompilationDatabaseOptions(),
                build_opts.FlagsOptions(),
                build_opts.OutputOptions(),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.ContentUidsOptions(),
                build_opts.ToolsOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
            visible=(pm.my_platform() != 'win32'),
        )
        self['vscode-go'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.vscode_go.gen_vscode_workspace),
            description="[[bad]]ya ide vscode-go[[rst]] is deprecated, please use [[good]]ya ide vscode --go[[rst]] instead",
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                devtools.ya.ide.vscode_go.VSCodeGoOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                build_opts.FlagsOptions(),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.ContentUidsOptions(),
                build_opts.ToolsOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
        )
        self['vscode-py'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.vscode_py.gen_vscode_workspace),
            description="[[bad]]ya ide vscode-py[[rst]] is deprecated, please use [[good]]ya ide vscode --py3[[rst]] instead",
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                devtools.ya.ide.vscode_py.VSCodePyOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                build_opts.FlagsOptions(),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.ContentUidsOptions(),
                build_opts.ToolsOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
            visible=(pm.my_platform() != 'win32'),
        )
        self['vscode-ts'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.vscode_ts.gen_vscode_workspace),
            description=get_description('Generate VSCode TypeScript project.', ref_name='typescript'),
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                devtools.ya.ide.vscode_ts.VSCodeTypeScriptOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                build_opts.FlagsOptions(),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.ContentUidsOptions(),
                build_opts.ToolsOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
            visible=(pm.my_platform() != 'win32'),
        )
        self['vscode'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.vscode_all.gen_vscode_workspace),
            description=get_description('Generate VSCode multi-language project.', ref_name='multi'),
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True, prefetch=True)
            + [
                devtools.ya.ide.vscode.opts.VSCodeAllOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                bcd.CompilationDatabaseOptions(),
                build_opts.FlagsOptions(),
                build_opts.OutputOptions(),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.ContentUidsOptions(),
                build_opts.ToolsOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
        )
        self['venv'] = yarg.OptsHandler(
            action=devtools.ya.app.execute(devtools.ya.ide.venv.do_venv),
            description='Create or update python venv',
            opts=devtools.ya.ide.ide_common.ide_minimal_opts(targets_free=True)
            + [
                build_opts.BuildTypeOptions('release'),
                build_opts.BuildThreadsOptions(build_threads=None),
                build_opts.ExecutorOptions(),
                build_opts.FlagsOptions(),
                build_opts.IgnoreRecursesOptions(),
                build_opts.RebuildOptions(),
                devtools.ya.core.common_opts.BeVerboseOptions(),
                devtools.ya.core.common_opts.CrossCompilationOptions(),
                devtools.ya.ide.ide_common.YaExtraArgsOptions(),
                devtools.ya.ide.venv.VenvOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
            ],
            visible=(pm.my_platform() != 'win32'),
        )
        if app_config.in_house:
            self['fix-jb-fsnotifier'] = yarg.OptsHandler(
                action=devtools.ya.app.execute(devtools.ya.ide.fsnotifier.fix_fsnotifier),
                description='Replace fsnotifier for JB IDEs.',
                opts=[
                    devtools.ya.ide.fsnotifier.FixFsNotifierOptions(),
                    devtools.ya.core.common_opts.ShowHelpOptions(),
                    devtools.ya.core.common_opts.DumpDebugOptions(),
                    devtools.ya.core.common_opts.AuthOptions(),
                    devtools.ya.core.common_opts.YaBin3Options(),
                ],
            )
