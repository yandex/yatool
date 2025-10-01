import devtools.ya.test.const


class MetaInfo(object):

    @property
    def docker_images(self):
        return self._docker_images or []

    @property
    def meta_dict(self):
        return self._meta_dict

    @property
    def android_apk_test_activity(self):
        return self._android_apk_test_activity or ''

    @property
    def benchmark_opts(self):
        return self._benchmark_opts or []

    @property
    def binary_path(self):
        return self._binary_path

    @property
    def blob(self):
        return self._blob or ''

    @property
    def build_folder_path(self):
        if self._build_folder_path is None:
            raise ValueError('build_folder_path cannot be None')
        return self._build_folder_path

    @property
    def canonize_sub_path(self):
        return self._canonize_sub_path or ''

    @property
    def classpath(self):
        return self._classpath or ''

    @property
    def config_path(self):
        if self._config_path is None:
            raise ValueError('config_path cannot be None')
        return self._config_path

    @property
    def custom_dependencies(self):
        return self._custom_dependencies or ''

    @property
    def eslint_config_path(self):
        return self._eslint_config_path

    @property
    def fork_mode(self):
        return self._fork_mode or ''

    @property
    def fork_test_files(self):
        return self._fork_test_files

    @property
    def experimental_fork(self):
        return self._experimental_fork or ''

    @property
    def parallel_tests_on_yt_within_node(self):
        return self._parallel_tests_on_yt_within_node or ''

    @property
    def fuzz_dicts(self):
        return self._fuzz_dicts or ()

    @property
    def fuzz_opts(self):
        return self._fuzz_opts or ()

    @property
    def global_library_path(self):
        return self._global_library_path or ''

    @property
    def global_resources(self):
        return self._global_resources or {}

    @property
    def go_bench_timeout(self):
        return self._go_bench_timeout

    @property
    def strict_classpath_clash(self):
        return self._strict_classpath_clash

    @property
    def ignore_classpath_clash(self):
        return self._ignore_classpath_clash or ''

    @property
    def java_classpath_cmd_type(self):
        if self._java_classpath_cmd_type is None:
            raise ValueError('java_classpath_cmd_type cannot be None')
        return self._java_classpath_cmd_type

    @property
    def jdk_for_tests_resource_prefix(self):
        return self._jdk_for_tests_resource_prefix

    @property
    def jdk_latest_version(self):
        return self._jdk_latest_version

    @property
    def jdk_resource_prefix(self):
        return self._jdk_resource_prefix

    @property
    def ktlint_baseline_file(self):
        return self._ktlint_baseline_file

    @property
    def ktlint_ruleset(self):
        return self._ktlint_ruleset

    @property
    def ktlint_binary(self):
        if self._ktlint_binary is None:
            raise ValueError('ktlint_binary cannot be None')
        return self._ktlint_binary

    @property
    def lint_configs(self):
        return self._lint_configs or ()

    @property
    def lint_wrapper_script(self):
        if self._lint_wrapper_script is None:
            raise ValueError('lint_wrapper_script cannot be None')
        return self._lint_wrapper_script

    @property
    def lint_extra_params(self):
        return self._lint_extra_params or ()

    @property
    def lint_file_processing_time(self):
        return self._lint_file_processing_time

    @property
    def lint_global_resources_keys(self):
        return self._lint_global_resources_keys or ()

    @property
    def lint_name(self):
        if self._lint_name is None:
            raise ValueError('lint_name cannot be None')
        return self._lint_name

    @property
    def module_lang(self):
        return self._module_lang or devtools.ya.test.const.ModuleLang.UNKNOWN

    @property
    def no_check(self):
        return self._no_check or ()

    @property
    def nodejs_resource(self):
        return self._nodejs_resource

    @property
    def nodejs_root_var_name(self):
        return self._nodejs_root_var_name

    @property
    def nyc_resource(self):
        return self._nyc_resource

    @property
    def python_paths(self):
        return self._python_paths or ()

    @property
    def requirements(self):
        return self._requirements or ()

    @property
    def sbr_uid_ext(self):
        return self._sbr_uid_ext

    @property
    def script_rel_path(self):
        if self._script_rel_path is None:
            raise ValueError('script_rel_path cannot be None')
        return self._script_rel_path

    @property
    def size(self):
        return self._size

    @property
    def skip_test(self):
        return self._skip_test

    @property
    def source_folder_path(self):
        return self._source_folder_path

    @property
    def split_factor(self):
        return self._split_factor

    @property
    def tag(self):
        return self._tag or ()

    @property
    def test_classpath(self):
        if self._test_classpath is None:
            raise ValueError('test_classpath cannot be None')
        return self._test_classpath

    @property
    def test_classpath_deps(self):
        if self._test_classpath_deps is None:
            raise ValueError('test_classpath_deps cannot be None')
        return self._test_classpath_deps

    @property
    def test_cwd(self):
        return self._test_cwd

    @property
    def test_data(self):
        return self._test_data or ()

    @property
    def tested_project_filename(self):
        return self._tested_project_filename

    @property
    def tested_project_name(self):
        if self._tested_project_name is None:
            raise ValueError('tested_project_name cannot be None')
        return self._tested_project_name

    @property
    def test_env(self):
        return self._test_env

    @property
    def test_files(self):
        return self._test_files or ()

    @property
    def test_ios_device_type(self):
        return self._test_ios_device_type or ''

    @property
    def test_ios_runtime_type(self):
        return self._test_ios_runtime_type or ''

    @property
    def test_jar(self):
        if self._test_jar is None:
            raise ValueError('test_jar cannot be None')
        return self._test_jar

    @property
    def test_name(self):
        return self._test_name

    @property
    def test_partition(self):
        return self._test_partition

    @property
    def test_recipes(self):
        return self._test_recipes

    @property
    def test_runner_bin(self):
        return self._test_runner_bin

    @property
    def test_timeout(self):
        return self._test_timeout

    @property
    def t_jvm_args(self):
        return self._t_jvm_args or ()

    @property
    def ts_config_path(self):
        return self._ts_config_path

    @property
    def ts_stylelint_config(self):
        return self._ts_stylelint_config

    @property
    def ts_test_data_dirs(self):
        return self._ts_test_data_dirs

    @property
    def ts_test_data_dirs_rename(self):
        return self._ts_test_data_dirs_rename

    @property
    def ts_test_for_path(self):
        return self._ts_test_for_path

    @property
    def t_system_properties(self):
        return self._t_system_properties

    @property
    def use_arcadia_python(self):
        return self._use_arcadia_python

    @property
    def use_ktlint_old(self):
        return self._use_ktlint_old

    @property
    def yt_spec(self):
        return self._yt_spec or ()


class DartInfo(MetaInfo):
    def __init__(self, dart_info):
        # copy to prevent arbitrary modification of the origin
        self._meta_dict = dart_info.copy()

        self._android_apk_test_activity = dart_info.get('ANDROID_APK_TEST_ACTIVITY')
        self._benchmark_opts = dart_info.get('BENCHMARK-OPTS')
        self._binary_path = dart_info.get('BINARY-PATH')
        self._blob = dart_info.get("BLOB")
        self._build_folder_path = dart_info.get('BUILD-FOLDER-PATH')
        self._canonize_sub_path = dart_info.get('CANONIZE_SUB_PATH')
        self._classpath = dart_info.get('CLASSPATH')
        self._config_path = dart_info.get("CONFIG-PATH")
        self._custom_dependencies = dart_info.get('CUSTOM-DEPENDENCIES')
        self._docker_images = dart_info.get('DOCKER-IMAGES')
        self._eslint_config_path = dart_info.get("ESLINT_CONFIG_PATH")
        self._fork_mode = dart_info.get('FORK-MODE')
        self._fork_test_files = dart_info.get('FORK-TEST-FILES')
        self._fuzz_dicts = dart_info.get('FUZZ-DICTS')
        self._fuzz_opts = dart_info.get('FUZZ-OPTS')
        self._global_library_path = dart_info.get('GLOBAL-LIBRARY-PATH')
        self._global_resources = {k: v for k, v in dart_info.items() if k.endswith('_RESOURCE_GLOBAL')} or None
        self._go_bench_timeout = dart_info.get('GO_BENCH_TIMEOUT')
        self._strict_classpath_clash = dart_info.get("STRICT_CLASSPATH_CLASH")
        self._ignore_classpath_clash = dart_info.get("IGNORE_CLASSPATH_CLASH")
        self._java_classpath_cmd_type = dart_info.get("JAVA_CLASSPATH_CMD_TYPE")
        self._jdk_for_tests_resource_prefix = dart_info.get('JDK_FOR_TESTS')
        self._jdk_latest_version = dart_info.get('JDK_LATEST_VERSION')
        self._jdk_resource_prefix = dart_info.get('JDK_RESOURCE')
        self._ktlint_baseline_file = dart_info.get('KTLINT_BASELINE_FILE')
        self._ktlint_ruleset = dart_info.get('KTLINT_RULESET')
        self._ktlint_binary = dart_info.get("KTLINT_BINARY")
        self._lint_configs = dart_info.get("LINT-CONFIGS")
        self._lint_wrapper_script = dart_info.get("LINT-WRAPPER-SCRIPT")
        self._lint_extra_params = dart_info.get("LINT-EXTRA-PARAMS")
        self._lint_file_processing_time = dart_info.get("LINT-FILE-PROCESSING-TIME")
        self._lint_global_resources_keys = dart_info.get('LINT-GLOBAL-RESOURCES')
        self._lint_name = dart_info.get("LINT-NAME")
        self._module_lang = dart_info.get("MODULE_LANG")
        self._no_check = dart_info.get('NO-CHECK')
        self._nodejs_resource = dart_info.get(dart_info.get("NODEJS-ROOT-VAR-NAME"))
        self._nodejs_root_var_name = dart_info.get("NODEJS-ROOT-VAR-NAME")
        self._nyc_resource = dart_info.get(dart_info.get("NYC-ROOT-VAR-NAME"))
        self._python_paths = dart_info.get('PYTHON-PATHS')
        self._requirements = dart_info.get('REQUIREMENTS')
        self._sbr_uid_ext = dart_info.get('SBR-UID-EXT')
        self._script_rel_path = dart_info.get('SCRIPT-REL-PATH')
        self._size = dart_info.get('SIZE')
        self._skip_test = dart_info.get("SKIP_TEST")
        self._source_folder_path = dart_info.get('SOURCE-FOLDER-PATH')
        self._split_factor = dart_info.get('SPLIT-FACTOR')
        self._tag = dart_info.get('TAG')
        self._test_classpath = dart_info.get('TEST_CLASSPATH')
        self._test_classpath_deps = dart_info.get('TEST_CLASSPATH_DEPS')
        self._test_cwd = dart_info.get('TEST-CWD')
        self._test_data = dart_info.get('TEST-DATA')
        self._tested_project_filename = dart_info.get('TESTED-PROJECT-FILENAME')
        self._tested_project_name = dart_info.get('TESTED-PROJECT-NAME')
        self._test_env = dart_info.get('TEST-ENV')
        self._test_files = dart_info.get('TEST-FILES')
        self._test_ios_device_type = dart_info.get('TEST_IOS_DEVICE_TYPE')
        self._test_ios_runtime_type = dart_info.get('TEST_IOS_RUNTIME_TYPE')
        self._test_jar = dart_info.get('TEST_JAR')
        self._test_name = dart_info.get("TEST-NAME")
        self._test_partition = dart_info.get('TEST_PARTITION')
        self._experimental_fork = dart_info.get('TEST_EXPERIMENTAL_FORK')
        self._test_recipes = dart_info.get('TEST-RECIPES')
        self._test_runner_bin = dart_info.get('TEST-RUNNER-BIN')
        self._test_timeout = dart_info.get('TEST-TIMEOUT')
        self._t_jvm_args = dart_info.get('JVM_ARGS')
        self._ts_config_path = dart_info.get("TS_CONFIG_PATH")
        self._ts_stylelint_config = dart_info.get("TS_STYLELINT_CONFIG")
        self._ts_test_data_dirs = dart_info.get("TS-TEST-DATA-DIRS")
        self._ts_test_data_dirs_rename = dart_info.get("TS-TEST-DATA-DIRS-RENAME")
        self._ts_test_for_path = dart_info.get("TS-TEST-FOR-PATH")
        self._t_system_properties = dart_info.get('SYSTEM_PROPERTIES')
        self._use_arcadia_python = dart_info.get('USE_ARCADIA_PYTHON')
        self._use_ktlint_old = dart_info.get('USE_KTLINT_OLD')
        self._yt_spec = dart_info.get('YT-SPEC')
        self._parallel_tests_on_yt_within_node = dart_info.get('PARALLEL-TESTS-WITHIN-NODE-ON-YT')

    # test_name is sometimes set in suites' code
    @MetaInfo.test_name.setter
    def test_name(self, test_name):
        self._test_name = test_name
        self._meta_dict['TEST-NAME'] = self.test_name
