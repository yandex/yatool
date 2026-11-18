import copy
from collections import namedtuple

import devtools.ya.test.const


def _copy_dart(dart):
    # Parsed DART contains strings and lists of strings. Preserve list aliases,
    # but avoid recursively visiting immutable leaves. Keep general input support.
    if type(dart) is not dict:
        return copy.deepcopy(dart)
    for key, value in dart.items():
        if type(key) is not str or not (
            type(value) is str or type(value) is list and all(type(item) is str for item in value)
        ):
            return copy.deepcopy(dart)
    lists = {}
    result = {}
    for key, value in dart.items():
        if type(value) is list:
            identity = id(value)
            if identity not in lists:
                lists[identity] = value[:]
            value = lists[identity]
        result[key] = value
    return result


def _dart_get_global_resources(dart):
    return {k: v for k, v in dart.items() if k.endswith('_RESOURCE_GLOBAL')}


def _dart_get_resource_by_name(dart, name):
    return dart.get(dart.get(name))


# meta field name, default value, (function to retrieve a value from the source, its fixed *args)
_META_MAP = [
    ('meta_raw', None, (_copy_dart, ())),
    ('android_apk_test_activity', '', (dict.get, ('ANDROID_APK_TEST_ACTIVITY',))),
    ('benchmark_opts', list, (dict.get, ('BENCHMARK-OPTS',))),
    ('binary_path', None, (dict.get, ('BINARY-PATH',))),
    ('blob', '', (dict.get, ('BLOB',))),
    ('build_folder_path', None, (dict.get, ('BUILD-FOLDER-PATH',))),
    ('canonize_sub_path', '', (dict.get, ('CANONIZE_SUB_PATH',))),
    ('classpath', '', (dict.get, ('CLASSPATH',))),
    ('custom_dependencies', '', (dict.get, ('CUSTOM-DEPENDENCIES',))),
    ('docker_images', list, (dict.get, ('DOCKER-IMAGES',))),
    ('experimental_fork', '', (dict.get, ('TEST_EXPERIMENTAL_FORK',))),
    ('fork_mode', '', (dict.get, ('FORK-MODE',))),
    ('fork_test_files', None, (dict.get, ('FORK-TEST-FILES',))),
    ('fuzz_dicts', (), (dict.get, ('FUZZ-DICTS',))),
    ('fuzz_opts', (), (dict.get, ('FUZZ-OPTS',))),
    ('global_library_path', '', (dict.get, ('GLOBAL-LIBRARY-PATH',))),
    ('global_resources', dict, (_dart_get_global_resources, ())),
    ('go_bench_timeout', None, (dict.get, ('GO_BENCH_TIMEOUT',))),
    ('ignore_classpath_clash', '', (dict.get, ('IGNORE_CLASSPATH_CLASH',))),
    ('java_classpath_cmd_type', None, (dict.get, ('JAVA_CLASSPATH_CMD_TYPE',))),
    ('jdk_for_tests_resource_prefix', None, (dict.get, ('JDK_FOR_TESTS',))),
    ('jdk_latest_version', None, (dict.get, ('JDK_LATEST_VERSION',))),
    ('jdk_resource_prefix', None, (dict.get, ('JDK_RESOURCE',))),
    ('ktlint_baseline_file', None, (dict.get, ('KTLINT_BASELINE_FILE',))),
    ('ktlint_binary', None, (dict.get, ('KTLINT_BINARY',))),
    ('ktlint_ruleset', None, (dict.get, ('KTLINT_RULESET',))),
    ('lint_configs', (), (dict.get, ('LINT-CONFIGS',))),
    ('lint_extra_params', (), (dict.get, ('LINT-EXTRA-PARAMS',))),
    ('lint_file_processing_time', None, (dict.get, ('LINT-FILE-PROCESSING-TIME',))),
    ('lint_global_resources_keys', (), (dict.get, ('LINT-GLOBAL-RESOURCES',))),
    ('lint_name', None, (dict.get, ('LINT-NAME',))),
    ('lint_wrapper_script', None, (dict.get, ('LINT-WRAPPER-SCRIPT',))),
    ('module_lang', devtools.ya.test.const.ModuleLang.UNKNOWN, (dict.get, ('MODULE_LANG',))),
    ('no_check', (), (dict.get, ('NO-CHECK',))),
    ('nodejs_resource', None, (_dart_get_resource_by_name, ('NODEJS-ROOT-VAR-NAME',))),
    ('nodejs_root_var_name', None, (dict.get, ('NODEJS-ROOT-VAR-NAME',))),
    ('nyc_resource', None, (_dart_get_resource_by_name, ('NYC-ROOT-VAR-NAME',))),
    ('parallel_tests_within_node_workers', '', (dict.get, ('PARALLEL-TESTS-WITHIN-NODE-WORKERS',))),
    ('python_paths', (), (dict.get, ('PYTHON-PATHS',))),
    ('requirements', (), (dict.get, ('REQUIREMENTS',))),
    ('sbr_uid_ext', None, (dict.get, ('SBR-UID-EXT',))),
    ('script_rel_path', None, (dict.get, ('SCRIPT-REL-PATH',))),
    ('size', None, (dict.get, ('SIZE',))),
    ('skip_test', None, (dict.get, ('SKIP_TEST',))),
    ('source_folder_path', None, (dict.get, ('SOURCE-FOLDER-PATH',))),
    ('split_factor', None, (dict.get, ('SPLIT-FACTOR',))),
    ('strict_classpath_clash', None, (dict.get, ('STRICT_CLASSPATH_CLASH',))),
    ('t_jvm_args', (), (dict.get, ('JVM_ARGS',))),
    ('t_system_properties', None, (dict.get, ('SYSTEM_PROPERTIES',))),
    ('tag', (), (dict.get, ('TAG',))),
    ('test_classpath', None, (dict.get, ('TEST_CLASSPATH',))),
    ('test_classpath_deps', None, (dict.get, ('TEST_CLASSPATH_DEPS',))),
    ('test_cwd', None, (dict.get, ('TEST-CWD',))),
    ('test_data', (), (dict.get, ('TEST-DATA',))),
    ('test_env', None, (dict.get, ('TEST-ENV',))),
    ('test_files', (), (dict.get, ('TEST-FILES',))),
    ('test_ios_device_type', '', (dict.get, ('TEST_IOS_DEVICE_TYPE',))),
    ('test_ios_runtime_type', '', (dict.get, ('TEST_IOS_RUNTIME_TYPE',))),
    ('test_jar', None, (dict.get, ('TEST_JAR',))),
    ('test_name', None, (dict.get, ('TEST-NAME',))),
    ('test_partition', None, (dict.get, ('TEST_PARTITION',))),
    ('test_recipes', None, (dict.get, ('TEST-RECIPES',))),
    ('test_persistent_recipes', None, (dict.get, ('TEST-PERSISTENT-RECIPES',))),
    ('test_runner_bin', None, (dict.get, ('TEST-RUNNER-BIN',))),
    ('test_timeout', None, (dict.get, ('TEST-TIMEOUT',))),
    ('tested_project_filename', None, (dict.get, ('TESTED-PROJECT-FILENAME',))),
    ('tested_project_name', None, (dict.get, ('TESTED-PROJECT-NAME',))),
    ('ts_test_for_path', None, (dict.get, ('TS-TEST-FOR-PATH',))),
    ('ts_check_script_name', None, (dict.get, ('TS-CHECK-SCRIPT-NAME',))),
    ('ts_check_command', None, (dict.get, ('TS-CHECK-COMMAND',))),
    ('ts_check_has_coverage', None, (dict.get, ('TS-CHECK-HAS-COVERAGE',))),
    ('ts_check_type', None, (dict.get, ('TS-CHECK-TYPE',))),
    ('use_arcadia_python', None, (dict.get, ('USE_ARCADIA_PYTHON',))),
    ('use_ktlint_old', None, (dict.get, ('USE_KTLINT_OLD',))),
    ('yt_spec', (), (dict.get, ('YT-SPEC',))),
]


MetaInfo = namedtuple('MetaInfo', tuple(m[0] for m in _META_MAP))


def _make_meta_from_mapping(dart):
    data = {}
    for m in _META_MAP:
        field, default, (getter, args) = m[0], m[1], m[2]
        value = getter(dart, *args)
        if value is not None:
            data[field] = value
        else:
            data[field] = default() if callable(default) else default
    return MetaInfo(**data)


# Compile the field table once. Most declarations provide only a small subset
# of fields; construct fresh mutable defaults and evaluate indirect resources.
_META_DEFAULTS = tuple(None if callable(default) else default for _, default, _ in _META_MAP)
_META_FACTORIES = [(index, default) for index, (_, default, _) in enumerate(_META_MAP) if callable(default)]
_META_FIELDS = {}
_META_COMPUTED = []
for _index, (_, _, (_getter, _args)) in enumerate(_META_MAP):
    if _getter is dict.get and len(_args) == 1:
        _META_FIELDS.setdefault(_args[0], []).append(_index)
    else:
        _META_COMPUTED.append((_index, _getter, _args))


def make_meta_from_dart(dart):
    if type(dart) is not dict:
        return _make_meta_from_mapping(dart)
    values = list(_META_DEFAULTS)
    for key, value in dart.items():
        if value is not None:
            for index in _META_FIELDS.get(key, ()):
                values[index] = value
    for index, getter, args in _META_COMPUTED:
        value = getter(dart, *args)
        if value is not None:
            values[index] = value
    for index, factory in _META_FACTORIES:
        if values[index] is None:
            values[index] = factory()
    return MetaInfo._make(values)


def meta_dart_replace_test_name(meta, value):
    # FIXME: Delete this function. Meta must be created once and be read only.
    new_meta = meta._replace(test_name=value)
    new_meta.meta_raw['TEST-NAME'] = value
    return new_meta
