import collections
import copy
import itertools
import logging
import os
import random
import re
import shutil
import time
import traceback

import six

import app_config
import devtools.ya.build.build_opts
import devtools.ya.build.targets_deref
import devtools.ya.build.ya_make
import devtools.ya.build.genconf as bg
import devtools.ya.core.common_opts
import devtools.ya.core.config
import devtools.ya.core.error
import devtools.ya.core.profiler
import devtools.ya.core.stage_tracer as stage_tracer
import devtools.ya.core.yarg
import devtools.ya.handlers.package.opts as package_opts
import devtools.ya.test.opts as test_opts
import exts.fs
import exts.hashing
import exts.os2
import exts.path2
import exts.tmp
import exts.yjson as json
import package
import package.aar
import package.artifactory
import package.debian
import package.docker
import package.fs_util
import package.npm
import package.postprocessor
import package.process
import package.rpm
import package.source
import package.squashfs
import package.tarball
import package.vcs
import package.wheel
from package.package_tree import load_package, get_tree_info
from devtools.ya.package import const
from yalibrary import find_root
from yalibrary.tools import tool, UnsupportedToolchain, UnsupportedPlatform, ToolNotFoundException, ToolResolveException
from package.utils import list_files_from, timeit

if app_config.in_house:
    import package.sandbox_source
    import package.sandbox_postprocessor

    from yalibrary.upload import uploader, mds_uploader
    from devtools.ya.yalibrary.yandex.sandbox.misc.fix_logging import fix_logging

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("ya-package")

SOURCE_ELEMENTS = {
    'BUILD_OUTPUT': package.source.BuildOutputSource,
    'RELATIVE': package.source.RelativeSource,
    'DIRECTORY': package.source.DirectorySource,
    'ARCADIA': package.source.ArcadiaSource,
    'TEMP': package.source.TempSource,
    'SYMLINK': package.source.SymlinkSource,
    'INLINE': package.source.InlineFileSource,
}

POSTPROCESS_ELEMENTS = {
    'BUILD_OUTPUT': package.postprocessor.BuildOutputPostprocessor,
    'ARCADIA': package.postprocessor.ArcadiaPostprocessor,
    'TEMP': package.postprocessor.TempPostprocessor,
}

REQUIRED_META_FIELDS = [
    "name",
    "maintainer",
    "version",
]

if app_config.in_house:
    SOURCE_ELEMENTS['SANDBOX_RESOURCE'] = package.sandbox_source.SandboxSource
    POSTPROCESS_ELEMENTS['SANDBOX_RESOURCE'] = package.sandbox_postprocessor.SandboxPostprocessor


class YaPackageException(Exception):
    mute = True


class YaPackageBuildException(YaPackageException):
    mute = True

    def __init__(self, original_error_code: int):
        self.original_error_code = original_error_code


class YaPackageTestException(YaPackageException):
    mute = True


class PackageFileNotFoundException(YaPackageException):
    def __init__(self, *args, **kwargs):
        super(PackageFileNotFoundException, self).__init__(*args)
        self.missing_file_path = kwargs["path"]


def get_package_file(arcadia_root, package_file):
    if os.path.exists(exts.path2.abspath(package_file)):
        return exts.path2.abspath(package_file)

    abs_path = os.path.join(arcadia_root, package_file)
    if os.path.exists(abs_path):
        return os.path.normpath(abs_path)

    raise PackageFileNotFoundException('Package path {} cannot be found'.format(package_file), path=package_file)


TYPES_FOR_BUILDING = ["program", "package"]
DEFAULT_BUILD_KEY = None


def stage_started(stage_name):
    devtools.ya.core.profiler.profile_step_started(stage_name)


def stage_finished(stage_name):
    devtools.ya.core.profiler.profile_step_finished(stage_name)


@timeit
def _do_build(build_info, params, arcadia_root, app_ctx, parsed_package, formatters):
    targets = [t.format(**formatters) for t in build_info['targets']]
    build_key = build_info.get("build_key", "")
    build_key_str = "[[alt1]][{}][[rst]] ".format(build_key) if build_key else ""
    package.display.emit_message('{}Building targets: [[imp]]{}'.format(build_key_str, ' '.join(targets)))

    # TODO: This is very bad. Need to automatically copy all copiable parameters from params to merged_opts

    merged_opts = devtools.ya.core.yarg.merge_opts(devtools.ya.build.build_opts.ya_make_options())
    merged_opts.export_to_maven = build_info.get("maven-export", False)
    merged_opts.dump_sources = build_info.get("sources", False)
    merged_opts.disable_flake8_migrations = params.disable_flake8_migrations

    merged_opts.add_result = params.add_result
    merged_opts.add_host_result = params.add_host_result
    merged_opts.replace_result = params.replace_result
    merged_opts.all_outputs_to_result = params.all_outputs_to_result
    merged_opts.add_binaries_to_results = params.add_binaries_to_results

    merged_opts.add_modules_to_results = params.add_modules_to_results
    merged_opts.gen_renamed_results = params.gen_renamed_results
    merged_opts.strip_packages_from_results = params.strip_packages_from_results
    merged_opts.strip_binary_from_results = params.strip_binary_from_results

    build_options = merged_opts.initialize([])

    build_options.build_targets = [os.path.join(arcadia_root, t) for t in targets]

    build_options.build_type = build_info.get("build_type", params.build_type)
    build_options.sanitize = build_info.get("sanitize", params.sanitize)
    build_options.sanitizer_flags = build_info.get("sanitizer_flags", params.sanitizer_flags)
    build_options.lto = build_info.get("lto", params.lto)
    build_options.thinlto = build_info.get("thinlto", params.thinlto)
    build_options.force_build_depends = build_info.get("force_build_depends", params.force_build_depends)
    build_options.ignore_recurses = build_info.get("ignore_recurses", params.ignore_recurses)
    build_options.musl = build_info.get("musl", params.musl)
    build_options.sanitize_coverage = build_info.get("sanitize_coverage", params.sanitize_coverage)
    build_options.use_afl = build_info.get("use_afl", params.use_afl)
    build_options.race = build_info.get("race", params.race)

    build_options.pgo_user_path = build_info.get("pgo_use", params.pgo_user_path)

    build_options.build_threads = params.build_threads
    build_options.output_root = build_info["output_root"]
    build_options.create_symlinks = False
    build_options.keep_temps = params.keep_temps
    build_options.clear_build = params.clear_build
    build_options.vcs_file = params.vcs_file
    build_options.be_verbose = params.be_verbose
    build_options.custom_build_directory = params.custom_build_directory
    if app_config.in_house:
        build_options.use_distbuild = params.use_distbuild
        build_options.distbuild_patch = params.distbuild_patch
        build_options.dist_priority = params.dist_priority
        build_options.cluster_type = params.cluster_type
        build_options.download_artifacts = True
        build_options.distbuild_pool = params.distbuild_pool
    else:
        # no distbuild no cry for opensource
        build_options.use_distbuild = False

    build_options.stat_only_report_file = params.stat_only_report_file
    build_options.ymake_bin = params.ymake_bin
    build_options.prefetch = params.prefetch

    build_options.host_platform = params.host_platform
    build_options.host_flags = copy.deepcopy(params.host_flags)
    build_options.host_platform_flags = copy.deepcopy(params.host_platform_flags)
    for k, v in six.iteritems(params.flags):
        build_options.flags[k] = v

    if build_info.get("target-platforms"):
        target_platforms = [
            devtools.ya.core.common_opts.CrossCompilationOptions.make_platform(p)
            for p in build_info["target-platforms"]
        ]
        if params.target_platforms and params.target_platforms != target_platforms:
            raise YaPackageException(
                "Cannot choose target platforms between passed via ya package --target-platform and specified in json"
            )
    else:
        target_platforms = params.target_platforms

    if 'flags' in build_info:
        flags_list = []
        if target_platforms:
            for tp in target_platforms:
                if 'flags' in tp:
                    flags_list.append(tp['flags'])
        else:
            flags_list.append(build_options.flags)

        for flag in build_info['flags']:
            for flags in flags_list:
                flag_name = flag['name'].format(**formatters)
                flag_value = flag['value'].format(**formatters)
                flags[flag_name] = flag_value

    build_options.target_platforms = target_platforms

    build_options.build_results_report_file = params.build_results_report_file
    build_options.build_report_type = params.build_report_type
    build_options.build_results_resource_id = params.build_results_resource_id
    build_options.build_results_report_tests_only = params.build_results_report_tests_only
    if params.json_line_report_file:
        base, ext = os.path.splitext(params.json_line_report_file)
        build_options.json_line_report_file = '{base}_{name}{hash}{ext}'.format(
            base=base,
            name=parsed_package["meta"]["name"],
            hash=f'_{exts.hashing.md5_value(build_key)}' if build_key else '',
            ext=ext,
        )
    build_options.report_skipped_suites = params.report_skipped_suites
    build_options.report_skipped_suites_only = params.report_skipped_suites_only
    build_options.use_links_in_report = params.use_links_in_report

    build_options.dump_graph = params.dump_graph

    build_options.run_tests = params.run_tests
    build_options.cache_tests = params.cache_tests
    build_options.test_env = params.test_env
    if params.run_tests:
        build_options.print_test_console_report = True
        if params.junit_path:
            # FIXME: The file is overwritten by subsequent builds
            # if there are more than one build or package
            build_options.junit_path = params.junit_path
        else:
            filename = 'junit_{}'.format(parsed_package["meta"]["name"])
            if build_key:
                filename += '_{}'.format(exts.hashing.md5_value(build_key))
            build_options.junit_path = os.path.join(os.getcwd(), '{}.xml'.format(filename))
        # There might be a test which depends on program A which could be a required output for the package
        # and if this test is skipped, ya will strip out program A from a build graph.
        # There is not way to mark required binaries for package in the current version of graph,
        # so just don't strip them out
        build_options.strip_skipped_test_deps = False
        build_options.strip_idle_build_results = False
    build_options.test_tool_bin = params.test_tool_bin
    build_options.profile_test_tool = params.profile_test_tool

    if 'add-result' in build_info:
        build_options.add_result.extend(build_info['add-result'])

    build_options.dump_sources = True
    build_options.javac_flags = {f["name"]: f["value"] for f in build_info.get("javac_flags", [])}

    build_options.print_statistics = True
    build_options.statistics_out_dir = params.statistics_out_dir

    for i in [
        "auto_clean_results_cache",
        "build_cache",
        "build_cache_conf",
        "build_cache_master",
        "cache_codec",
        "cache_size",
        "cache_stat",
        "custom_fetcher",
        "executor_address",
        "fetcher_params",
        "junit_args",
        "link_threads",
        "local_executor",
        "new_store",
        "new_store_ttl",
        "output_style",
        "pytest_args",
        "strip_cache",
        "strip_symlinks",
        "symlinks_ttl",
        "test_log_level",
        "test_params",
        "test_size_filters",
        "test_tags_filter",
        "test_threads",
        "test_type_filters",
        "tests_filters",
        "tests_retries",
        "tools_cache",
        "tools_cache_bin",
        "tools_cache_conf",
        "tools_cache_gl_conf",
        "tools_cache_ini",
        "tools_cache_master",
    ]:
        setattr(build_options, i, getattr(params, i))

    if version := getattr(params, "custom_version"):
        build_options.custom_version = version.format(**formatters)
    if release_version := getattr(params, "release_version"):
        build_options.release_version = release_version.format(**formatters)
    build_options.yt_store = params.yt_store
    build_options.yt_proxy = params.yt_proxy
    build_options.yt_dir = params.yt_dir
    build_options.yt_token = params.yt_token
    build_options.yt_readonly = params.yt_readonly
    build_options.yt_max_cache_size = params.yt_max_cache_size
    build_options.yt_store_ttl = params.yt_store_ttl
    build_options.yt_store_codec = params.yt_store_codec
    build_options.yt_store_threads = params.yt_store_threads
    build_options.yt_store_refresh_on_read = params.yt_store_refresh_on_read
    build_options.yt_create_tables = params.yt_create_tables
    build_options.yt_store_retry_time_limit = params.yt_store_retry_time_limit
    build_options.yt_store2 = params.yt_store2
    build_options.yt_store_init_timeout = params.yt_store_init_timeout
    build_options.yt_store_prepare_timeout = params.yt_store_prepare_timeout
    build_options.yt_store_crit = params.yt_store_crit
    # heater options
    build_options.yt_store_wt = params.yt_store_wt
    build_options.eager_execution = params.eager_execution
    build_options.yt_replace_result = params.yt_replace_result
    build_options.yt_replace_result_add_objects = params.yt_replace_result_add_objects
    build_options.dist_cache_evict_binaries = params.dist_cache_evict_binaries
    build_options.dist_cache_evict_bundles = params.dist_cache_evict_bundles

    build_options.bazel_remote_store = params.bazel_remote_store
    build_options.bazel_remote_baseuri = params.bazel_remote_baseuri
    build_options.bazel_remote_username = params.bazel_remote_username
    build_options.bazel_remote_password = params.bazel_remote_password
    build_options.bazel_remote_password_file = params.bazel_remote_password_file
    build_options.bazel_remote_readonly = params.bazel_remote_readonly

    build_options.sandbox_oauth_token = params.sandbox_oauth_token
    if app_config.in_house:
        build_options.use_new_distbuild_client = params.use_new_distbuild_client
    build_options.username = params.username

    if 'package_builds' not in app_ctx.dump_debug:
        app_ctx.dump_debug['package_builds'] = []

    app_ctx.dump_debug['package_builds'] += [(build_key, build_options.__dict__)]

    logger.debug("Build options %s", json.dumps(build_options.__dict__, sort_keys=True, indent=2))

    builder = devtools.ya.build.targets_deref.intercept(
        lambda x: devtools.ya.build.ya_make.YaMake(x, app_ctx), build_options
    )
    builder.go()

    logger.info("Build finished with exit code %d, tests: %s", builder.exit_code, build_options.run_tests)

    # TODO: Rewrite with statuses
    if not build_options.run_tests and builder.exit_code:
        msg = f"{build_key_str}Building targets: [[bad]]failed with exit code {builder.exit_code}"
    elif (
        build_options.run_tests
        and builder.exit_code
        and builder.exit_code
        not in (devtools.ya.core.error.ExitCodes.TEST_FAILED, devtools.ya.core.error.ExitCodes.INFRASTRUCTURE_ERROR)
    ):
        msg = f"{build_key_str}Building targets: [[bad]]failed with exit code {builder.exit_code}"
    elif build_options.run_tests and builder.exit_code in (
        devtools.ya.core.error.ExitCodes.TEST_FAILED,
        devtools.ya.core.error.ExitCodes.INFRASTRUCTURE_ERROR,
    ):
        msg = f"{build_key_str}Building targets: [[good]]done[[rst]], testing: [[bad]]failed"
    elif build_options.run_tests:
        msg = f"{build_key_str}Building targets: [[good]]done[[rst]], testing: [[good]]done"
    else:
        msg = f"{build_key_str}Building targets: [[good]]done[[rst]]"

    package.display.emit_message(msg)

    if builder.exit_code:
        if builder.exit_code == devtools.ya.core.error.ExitCodes.TEST_FAILED:
            if not params.ignore_fail_tests:
                # stop when tests are failed, and we wont ignore that ...
                raise YaPackageTestException()
        else:
            # ... or if build failed on any reason
            raise YaPackageBuildException(builder.exit_code)

    return builder.exit_code


@timeit
def prepare_package(
    format,
    result_dir,
    parsed_package,
    arcadia_root,
    builds,
    package_root,
    formaters,
    files_comparator,
    params,
):
    source_elements = []

    title = '=' * 20 + ' Package files ' + '=' * 20
    package.display.emit_message('[[imp]]{}'.format(title))

    temp = exts.fs.create_dirs(os.path.join(result_dir, 'temp-' + str(random.random())))

    if 'data' in parsed_package:
        for element in parsed_package['data']:
            element_type_name = element['source']['type']
            if element_type_name in SOURCE_ELEMENTS:
                element_type = SOURCE_ELEMENTS[element_type_name]
            else:
                raise YaPackageException('Unknown source element type {}'.format(element_type_name))

            package.display.emit_message('[[good]]{}[[rst]]: [[imp]]{}[[rst]]'.format('SOURCE', element_type_name))

            element = element_type(
                arcadia_root,
                builds,
                element,
                result_dir,
                package_root,
                os.path.basename(temp),
                formaters,
                files_comparator,
                params,
            )
            with stager.scope(str(element), tag="prepare-data-element"):
                element.prepare(apply_attributes=format != const.PackageFormat.DEBIAN)

            source_elements.append(element)
    if 'postprocess' in parsed_package:
        with exts.tmp.temp_dir('ya-package-postprocess') as workspace:
            for element in parsed_package['postprocess']:
                element_type_name = element['source']['type']
                if element_type_name in POSTPROCESS_ELEMENTS:
                    element_type = POSTPROCESS_ELEMENTS[element_type_name]
                else:
                    raise YaPackageException('Unknown postprocess element type {}'.format(element_type_name))

                package.display.emit_message(
                    '[[good]]{}[[rst]]: [[imp]]{}[[rst]]'.format('POSTPROCESS', element_type_name)
                )

                element = element_type(
                    arcadia_root,
                    builds,
                    element,
                    result_dir,
                    temp,
                    workspace,
                    params,
                    formaters,
                )
                with stager.scope(str(element), "run-postprocess-element"):
                    element.run()

    package.fs_util.cleanup(temp)

    package.display.emit_message('[[imp]]{}'.format('=' * len(title)))
    return source_elements


def is_application(path):
    assert os.path.isfile(path)
    try:
        import magic

        return 'application' in magic.from_file(path, mime=True)
    except ImportError:
        # use heuristics like here: https://stackoverflow.com/questions/898669/how-can-i-detect-if-a-file-is-binary-non-text-in-python
        textchars = bytearray({7, 8, 9, 10, 12, 13, 27} | set(range(0x20, 0x100)) - {0x7F})

        def is_binary_string(bytes):
            return bool(bytes.translate(None, textchars))

        return is_binary_string(open(path, 'rb').read(1024))


def get_tool_path(name, tool_platform=None):
    # fallback to system tool on every unknown situation
    if not tool_platform:
        return name

    try:
        tool_path = tool(name, None, target_platform=tool_platform.upper())
    except (UnsupportedToolchain, UnsupportedPlatform, ToolNotFoundException, ToolResolveException) as e:
        package.display.emit_message(
            'Not found toolchain for tool {} on platform {}: {}. Use the system one.'.format(
                name, tool_platform, str(e)
            )
        )
        return name

    if exts.windows.on_win() and not tool_path.endswith('.exe'):  # XXX: hack. Think about ya.conf.json format
        logger.debug('Rename tool for win: %s', tool_path)
        tool_path += '.exe'
    return tool_path


def strip_binary(executable_name, debug_file_name=None, tool_platform=None, full_strip=False):
    objcopy_tool = get_tool_path('objcopy', tool_platform)
    strip_tool = get_tool_path('strip', tool_platform)

    # Detach debug symbols from file
    if debug_file_name is not None:
        package.process.run_process(
            objcopy_tool,
            [
                '--only-keep-debug',
                executable_name,
                debug_file_name,
            ],
        )

    # Do strip binary
    if full_strip:
        strip_args = [executable_name]
    else:
        strip_args = ['--strip-debug', executable_name]
    package.process.run_process(strip_tool, strip_args)

    # Attach debug file info to stripped binary
    if debug_file_name is not None:
        package.process.run_process(
            objcopy_tool,
            [
                '--remove-section=.gnu_debuglink',
                '--add-gnu-debuglink',
                debug_file_name,
                executable_name,
            ],
        )


def get_platform_from_build_info(tool_platform):
    if isinstance(tool_platform, dict):
        return tool_platform.get('platform_name', '')
    if isinstance(tool_platform, six.string_types):
        return tool_platform
    return ''


def guess_tool_platform(filename, tool_platforms):
    if not tool_platforms:
        return None
    if len(tool_platforms) == 1:
        return get_platform_from_build_info(tool_platforms[0])
    try:
        extension = os.path.splitext(filename)[1].upper()
        for platform in tool_platforms:
            platform_str = get_platform_from_build_info(platform).upper()
            if platform_str in extension:
                return platform_str
        return get_platform_from_build_info(tool_platforms[0])
    except IndexError:
        return get_platform_from_build_info(tool_platforms[0])


@timeit
def strip_binaries(result_dir, debug_dir, source_elements, full_strip=False, create_dbg=False):
    for element in source_elements:
        if element.data['source']['type'] != 'BUILD_OUTPUT':
            logger.debug('Strip binaries only from BUILD_OUTPUT section. Skip %s', element.data)
            continue

        if element.opts.target_platforms:
            tool_platforms = [tp['platform_name'] for tp in element.opts.target_platforms]
        else:
            build_info = element.builds[element.data['source'].get('build_key', None)]
            tool_platforms = build_info.get('target-platforms', [])

        logger.debug(
            'Strip binaries from paths [%s] with toolchains %s', ', '.join(element.destination_paths), tool_platforms
        )
        tool_platform = guess_tool_platform(element.data['source']['path'], tool_platforms)
        for destination_path in list_files_from(element.destination_paths):
            try_strip_file(
                result_dir,
                debug_dir,
                destination_path,
                tool_platform=tool_platform,
                full_strip=full_strip,
                create_dbg=create_dbg,
            )


def try_strip_file(
    result_dir,
    debug_dir,
    destination_path,
    tool_platform=None,
    full_strip=False,
    create_dbg=False,
):
    if os.path.isfile(destination_path) and os.access(destination_path, os.X_OK) and is_application(destination_path):
        executable_name = os.path.basename(destination_path)
        executable_relative_dir = os.path.dirname(os.path.relpath(destination_path, result_dir))

        with exts.os2.change_dir(os.path.dirname(destination_path)):
            if create_dbg:
                debug_file_name = os.path.join(debug_dir, executable_relative_dir, executable_name + ".debug")
                exts.fs.create_dirs(os.path.dirname(debug_file_name))
            else:
                debug_file_name = None

            try:
                strip_binary(
                    executable_name, debug_file_name=debug_file_name, tool_platform=tool_platform, full_strip=full_strip
                )
            except package.packager.YaPackageException as e:
                package.display.emit_message('[[bad]]Strip failed: {}[[rst]]'.format(e))


def is_old_format(package_data):
    for data_el in package_data.get("data", []):
        if 'src' in data_el:
            return True
        if 'source' in data_el:
            return False
    return False


def verify_rpm_package_meta(package_meta):
    required_keys = ('name', 'version', 'homepage', 'rpm_license', 'rpm_release')
    missing_keys = [key for key in required_keys if key not in package_meta]

    if missing_keys:
        err_msg = "The following fields: {fields} are mandatory in meta section for building rpm package."
        raise KeyError(err_msg.format(fields=", ".join(missing_keys)))


def format_package_meta(meta_value, formatters):
    try:
        meta_value = meta_value.format(**formatters)
    except KeyError as e:
        raise Exception("Can not substitute `{}`. val: {} formatters: {}".format(e, meta_value, formatters))
    return meta_value


@timeit
def create_package(package_context, output_root, builds):
    arcadia_root = package_context.arcadia_root
    package_root = package_context.package_root
    parsed_package = package_context.parsed_package
    params = package_context.params
    formatters = package_context.formatters

    package_meta = parsed_package['meta']
    package_data = parsed_package['data']

    for field in REQUIRED_META_FIELDS:
        if field not in package_meta:
            raise YaPackageException('Meta field {} is required'.format(field))

    package_version = package_context.version
    package_name = package_context.package_name

    for key, value in six.iteritems(package_meta):
        if isinstance(value, six.string_types):
            package_meta[key] = format_package_meta(value, formatters)

    if params.package_output:
        result_dir = params.package_output
        exts.fs.ensure_dir(result_dir)
    else:
        result_dir = os.getcwd()

    package_path = debug_package_path = None
    abs_package_root = os.path.join(arcadia_root, package_root)

    # package format: command line -> default format fixed in json -> general default format is tar
    def get_format_from_meta():
        str_format = parsed_package['meta'].get('default-format', None)
        if str_format == 'raw-package':
            return const.PackageFormat.TAR, True
        return str_format, False

    def get_format_from_cmd():
        if params.format:
            return params.format, params.raw_package
        if params.raw_package:
            return const.PackageFormat.TAR, True
        return None, False

    format_from_meta, build_raw_from_meta = get_format_from_meta()
    format_from_cmd, build_raw_from_cmd = get_format_from_cmd()

    package_format = format_from_cmd or format_from_meta or const.PackageFormat.TAR
    build_raw_package = build_raw_from_cmd if format_from_cmd else build_raw_from_meta

    files_comparator = package.source.FilesComparator()

    with exts.tmp.temp_dir() as temp_work_dir:
        try:
            content_dir = exts.fs.create_dirs(os.path.join(temp_work_dir, '.content'))

            source_elements = prepare_package(
                package_format,
                content_dir,
                parsed_package,
                arcadia_root,
                builds,
                abs_package_root,
                formatters,
                files_comparator,
                params,
            )

            create_dbg = False

            if params.strip or params.full_strip:
                debug_dir = exts.fs.create_dirs(os.path.join(temp_work_dir, '.debug'))
                strip_binaries(
                    temp_work_dir,
                    debug_dir,
                    source_elements,
                    full_strip=params.full_strip,
                    create_dbg=params.create_dbg,
                )
                if params.create_dbg:
                    debug_dir = os.path.join(debug_dir, '.content')
                    create_dbg = os.path.exists(debug_dir)
                    if not create_dbg:
                        logger.debug("Debug content dir for %s does not exist", package_name)

            package.display.emit_message(
                'Creating [[imp]]{}[[rst]] package [[imp]]{}[[rst]] version [[imp]]{}'.format(
                    package_format, package_name, package_version
                )
            )

            timestamp_started = time.time()
            should_push_to_artifactory = params.artifactory and params.publish_to
            if should_push_to_artifactory:
                artifactory_settings = package.artifactory.get_artifactory_settings(
                    package_context.arcadia_root, package_context.params.publish_to
                )
                package.artifactory.publish_to_artifactory(
                    content_dir,
                    package_version,
                    artifactory_settings=artifactory_settings,
                    password_path=params.artifactory_password_path,
                )
                params.publish_to = []
            if package_format == const.PackageFormat.TAR:
                package.display.emit_message('Package version: [[imp]]{}'.format(package_version))
                if build_raw_package:
                    package_path = package_context.get_raw_package_path()
                    exts.fs.ensure_removed(package_path)
                    shutil.move(content_dir, package_path)
                    package.display.emit_message(
                        'Package content is stored in [[imp]]{}'.format(os.path.abspath(package_path))
                    )
                else:
                    stage_started("create_tarball_package")
                    package_path = package.tarball.create_tarball_package(
                        result_dir,
                        content_dir,
                        package_context.resolve_filename(extra={"package_ext": "tar"}),
                        compress=params.compress_archive,
                        codec=params.codec,
                        threads=params.build_threads,
                        compression_filter=params.compression_filter,
                        compression_level=params.compression_level,
                        stable_archive=params.stable_archive,
                    )
                    stage_finished("create_tarball_package")

                if create_dbg:
                    package_filename = package_context.resolve_filename(
                        extra={"package_ext": "tar", "package_name": package_context.package_name + '-dbg'}
                    )

                    stage_started("create_tarball_package-dbg")
                    debug_package_path = package.tarball.create_tarball_package(
                        result_dir,
                        debug_dir,
                        package_filename,
                        compress=params.compress_archive,
                        codec=params.codec,
                        threads=params.build_threads,
                        compression_filter=params.compression_filter,
                        compression_level=params.compression_level,
                        stable_archive=params.stable_archive,
                    )
                    stage_finished("create_tarball_package-dbg")

            elif package_format == const.PackageFormat.AAR:
                package_path = package.aar.create_aar_package(
                    result_dir,
                    content_dir,
                    package_context,
                    compress=params.compress_archive,
                    publish_to_list=params.publish_to,
                )
            elif package_format == const.PackageFormat.SQUASHFS:
                package_path = package.squashfs.create_squashfs_package(
                    result_dir,
                    content_dir,
                    package_context,
                    compression_filter=params.squashfs_compression_filter,
                )
            elif package_format == const.PackageFormat.DEBIAN:
                if params.build_debian_scripts:
                    debian_dir = os.path.join(output_root, package_root, 'debian')
                else:
                    debian_dir = os.path.join(abs_package_root, 'debian')
                debian_dir = package.debian.prepare_debian_folder(temp_work_dir, debian_dir)

                changelog_message = 'Created by ya package'
                change_log_path = os.path.join(debian_dir, "changelog")

                # XXX debug logging, see DEVTOOLSSUPPORT-11944
                change_log_is_file = False
                if params.change_log:
                    try:
                        logger.debug("type(params.change_log) = %s", type(params.change_log))
                        logger.debug("params.change_log = %s (%r)", params.change_log, params.change_log)
                        change_log_is_file = os.path.isfile(params.change_log)
                        logger.debug("isfile params.change_log = %s", change_log_is_file)
                        logger.debug("exists params.change_log = %s", os.path.exists(params.change_log))
                    except Exception:
                        logger.exception("Check failed, see DEVTOOLSSUPPORT-11944")

                if params.change_log and change_log_is_file:
                    exts.fs.copy_file(params.change_log, change_log_path)
                else:
                    if params.change_log:
                        changelog_message += "\n" + params.change_log

                if not re.match(r"\d.*", package_version):
                    raise YaPackageException(
                        "Invalid version '{}', version has to start with a digit".format(package_version)
                    )

                package.display.emit_message('Package version: [[imp]]{}'.format(package_version))

                if 'depends' in parsed_package['meta']:
                    parsed_package['meta']['depends'] = [
                        format_package_meta(dep, formatters) for dep in parsed_package['meta']['depends']
                    ]

                debug_package_name = package_name + '-dbg' if create_dbg else None

                noconffiles_all = parsed_package['meta'].get('noconffiles_all', True)

                package.debian.create_rules_file(debian_dir, source_elements, params, content_dir, noconffiles_all)
                if source_elements:
                    package.debian.create_install_file(debian_dir, source_elements, package_name)
                package.debian.create_compat_file(debian_dir)
                package.debian.create_control_file(
                    debian_dir, package_name, package_meta, params.arch_all, debug_package_name
                )

                if not params.build_debian_scripts:
                    package.debian.create_changelog_file(
                        debian_dir,
                        package_meta.get('source', package_name),
                        package_version,
                        params.debian_distribution,
                        changelog_message,
                        params.debian_force_bad_version,
                    )

                if create_dbg:
                    debug_install_file_name = os.path.join(debian_dir, debug_package_name + '.install')
                    with open(debug_install_file_name, 'w') as debug_install_file:
                        debug_install_file.write('.debug/.content/* /\n')

                package_path = package.debian.create_debian_package(
                    result_dir,
                    temp_work_dir,
                    package_context,
                    params.arch_all,
                    params.sign,
                    params.sign_debsigs,
                    params.key,
                    params.sloppy_deb,
                    params.publish_to,
                    params.force_dupload,
                    params.store_debian,
                    params.dupload_max_attempts,
                    params.dupload_no_mail,
                    params.ensure_package_published,
                    params.debian_arch,
                    params.debian_distribution,
                    params.debian_upload_token,
                    params.dist2_repo,
                    params.dist2_repo_pgp_private_key,
                    params.dist2_repo_reindex,
                    params.dist2_repo_s3_access_key,
                    params.dist2_repo_s3_bucket,
                    params.dist2_repo_s3_endpoint,
                    params.dist2_repo_s3_secret_key,
                )

            elif package_format == const.PackageFormat.RPM:
                rpm_build_version = package_version
                if package_context.params.custom_version and '-' in package_version:
                    rpm_build_version, rpm_build_release = package_version.split('-', 1)
                    package_meta.update({"rpm_release": rpm_build_release})
                verify_rpm_package_meta(package_meta)
                rpm_build_release = package_meta["rpm_release"]
                data_files = []
                for elem in package_data:
                    try:
                        path = elem['destination']['path']
                        if path.endswith('/'):
                            path += '*'
                        data_files.append(path)
                    except KeyError:
                        data_files.append(elem['destination']['archive'])
                data_files = '\n'.join(data_files) or '/*'
                data_files = data_files.format(**formatters)
                logger.debug("files for 'files' section in spec file: %s", data_files)
                spec_file = package.rpm.create_spec_file(
                    temp_work_dir,
                    package_name,
                    rpm_build_version,
                    rpm_build_release,
                    package_meta.get("rpm_buildarch"),
                    'package {} was built by ya package'.format(package_name),
                    package_meta['homepage'],
                    package_meta.get('depends'),
                    package_meta.get("pre-depends"),
                    package_meta.get("provides"),
                    package_meta.get("conflicts"),
                    package_meta.get("replaces"),
                    [package_context.resolve_filename(extra={"package_ext": "tar.gz"})],
                    package_meta['description'],
                    package_meta["rpm_license"],
                    data_files,
                    abs_package_root,
                )
                gz_file = package.rpm.create_gz_file(
                    package_context, temp_work_dir, content_dir, threads=params.build_threads
                )
                rpmbuild_dir = package.rpm.prepare_rpm_folder_structure(temp_work_dir, spec_file, gz_file)
                package_path = package.rpm.create_rpm_package(
                    temp_work_dir,
                    package_context,
                    rpmbuild_dir,
                    params.publish_to,
                    params.key,
                )
                package_version = "{0}-{1}".format(rpm_build_version, rpm_build_release)

            elif package_format == const.PackageFormat.DOCKER:
                package_path, info = package.docker.create_package(
                    params.docker_registry,
                    params.docker_repository,
                    params.docker_image_name,
                    package_context,
                    content_dir,
                    result_dir,
                    params.docker_save_image,
                    params.docker_push_image,
                    params.nanny_release,
                    params.docker_build_network,
                    params.docker_build_arg,
                    params.docker_no_cache,
                    params.docker_use_remote_cache,
                    params.docker_remote_image_version,
                    params.docker_export_cache_to_registry,
                    params.docker_dest_remote_image_version,
                    params.docker_platform,
                    params.docker_add_host,
                    params.docker_target,
                    params.docker_secrets,
                    params.docker_use_buildx,
                    params.docker_pull,
                    params.docker_labels,
                )
                package_context.set_context("docker_image", info.image_tag)
                if info.digest:
                    package_context.set_context("digest", info.digest)

            elif package_format == const.PackageFormat.NPM:
                package_path = package.npm.create_npm_package(
                    content_dir,
                    result_dir,
                    params.publish_to,
                    package_context,
                )

            elif package_format == const.PackageFormat.WHEEL:
                package_path = package.wheel.setup(
                    content_dir,
                    result_dir,
                    params.publish_to,
                    params.wheel_access_key_path,
                    params.wheel_secret_key_path,
                    params.wheel_python3,
                    params.wheel_platform,
                    params.wheel_limited_api,
                    package_version,
                )

            else:
                raise YaPackageException("Unknown packaging format '{}'".format(package_format))
            package.display.emit_message(
                'Package [[good]]successfully[[rst]] packed in [[imp]]{:.2f}s[[rst]]'.format(
                    time.time() - timestamp_started
                )
            )
        finally:
            if not params.cleanup and os.path.exists(temp_work_dir):
                result_temp = 'result.' + package_name + '.' + str(random.random())
                exts.fs.copytree3(temp_work_dir, result_temp, symlinks=True)
                package.display.emit_message('Result temp directory: [[imp]]{}'.format(result_temp))

        return package_name, package_version, package_path, debug_package_path


def update_params(parsed_package, package_params, filename):
    if 'params' not in parsed_package:
        return package_params

    logger.debug("Updating params from %s", filename)
    default_params = package_opts.PackageCustomizableOptions()
    new_params = {}

    for opt, value in parsed_package['params'].items():
        # Allow to redefine options only from PackageCustomizableOptions
        if hasattr(default_params, opt):
            def_value = getattr(default_params, opt)
            user_value = getattr(package_params, opt)
            if def_value is None or isinstance(value, type(def_value)):
                if isinstance(value, dict):
                    new_params[opt] = copy.deepcopy(value)
                    # User cli params always extends package.json's dict params
                    new_params[opt].update(user_value)
                    logger.debug(
                        "Dict option '%s' updated: %s (%r) -> %s (%r)",
                        opt,
                        user_value,
                        type(user_value),
                        new_params[opt],
                        type(value),
                    )
                elif def_value == user_value:
                    new_params[opt] = value
                    logger.debug(
                        "Option '%s' updated: %s (%r) -> %s (%r)", opt, user_value, type(user_value), value, type(value)
                    )
                else:
                    logger.info("Option '%s' from file is overwritten by command line argument: %s", opt, value)
            else:
                logger.warning("Option '%s' skipped due type mismatch: got %r expected %r", opt, value, def_value)
        else:
            logger.warning("Skipping unknown '%s' option from %s", opt, filename)

    return devtools.ya.core.yarg.merge_params(package_params, devtools.ya.core.yarg.Params(**new_params))


class PackageContext:
    def __init__(self, arcadia_root, package_file, params):
        self._arcadia_root = arcadia_root
        self._params = params
        self._context = {}
        self._formatters = {}
        self._spec = {}
        self._package_root = None
        self._version = None
        self._branch = None
        self._package_name = None
        self._package_path = None
        self._package_filename = None

        self._tree_info = None

        self._build_context(package_file)

    def _build_context(self, package_file):
        if package_file.startswith(self._arcadia_root):
            self._package_path = os.path.relpath(package_file, self._arcadia_root)
        else:
            self._package_path = package_file
        self._package_root = os.path.dirname(self._package_path)

        self._tree_info = get_tree_info(self._arcadia_root, self._package_path, self.params.include_traversal_variant)
        self._spec = load_package(self._tree_info)

        self._params = update_params(self._spec, self._params, self._package_path)

        self._branch = str(package.vcs.Branch(self._arcadia_root, self._params.arc_revision_means_trunk))
        self._package_name = self._read_spec_safe('meta', 'name', 'package_name')
        # package_filename has patterns in brackets {}
        # so format_package_meta wants to format it but we don't have package_ext yet
        self._package_filename = self._params.package_filename or self._spec['meta'].pop("package_filename", None)

        # Setup formatters, which are not part of context
        change_log = self._params.change_log or os.path.join(
            self._arcadia_root, self._package_root, 'debian', 'changelog'
        )
        self._formatters.update(
            {
                "changelog_version": package.debian.ChangeLogVersion(change_log),
                "package_root": self._package_root,
                "userdata": self._spec.get("userdata"),
            }
        )

        # Setup context
        self._context.update(
            {
                "branch": self._branch,
                "package_name": self._package_name,
                "package_path": self._package_path,
                "package_version": self._version,
                "revision": str(package.vcs.Revision(self._arcadia_root)),
                "revision_date": package.vcs.RevisionDate(self._arcadia_root),
                "sandbox_task_id": self._params.sandbox_task_id,
                "svn_revision": str(package.vcs.SvnRevision(self._arcadia_root)),
            }
        )

        self._version = self._calc_version()
        self._context.update(
            {
                "package_full_name": ".".join(x for x in (self._package_name, self._version)),
                "package_version": self._version,
            }
        )
        self._spec['meta']['version'] = self._version

    def _calc_version(self):
        if self._params.custom_version:
            ver = self._params.custom_version
        else:
            ver = self._read_spec_safe('meta', 'version', 'package_version')
        return format_package_meta(ver, self.formatters)

    def _read_spec_safe(self, p1, p2, default=None):
        return self._spec.get(p1, {}).get(p2, p2 if default is None else default)

    def update_context(self, kv):
        self._context.update(**kv)

    def set_context(self, k, v):
        self._context[k] = v

    @property
    def version(self):
        return self._version

    @property
    def branch(self):
        return self._branch

    @property
    def parsed_package(self):
        return self._spec

    @property
    def params(self):
        return self._params

    @property
    def package_root(self):
        return self._package_root

    @property
    def package_name(self):
        return self._package_name

    @property
    def package_filename(self):
        return self._package_filename

    @property
    def arcadia_root(self):
        return self._arcadia_root

    @property
    def formatters(self):
        # Context if part of formatters
        return {k: v for items in itertools.chain((self._formatters.items(), self._context.items())) for k, v in items}

    @property
    def context(self):
        return {k: v for k, v in self._context.items()}

    @property
    def package_path(self):
        return self._package_path

    @property
    def should_use_package_filename(self):
        return self.package_filename is not None

    @property
    def tree_info(self):
        return self._tree_info

    def resolve_filename(self, extra):
        filename_pattern = extra.get('pattern', self.package_filename)
        if not filename_pattern:
            filename_pattern = "{package_name}.{package_version}.{package_ext}"

        package_name = extra.get('package_name', self.package_name)
        package_version = extra.get('package_version', self.version)
        package_ext = extra.get('package_ext', '')

        return filename_pattern.format(
            package_name=package_name, package_version=package_version, package_ext=package_ext
        )

    def get_raw_package_path(self):
        if self.params.raw_package_path:
            return self.params.raw_package_path
        return '.'.join([self.package_name, self.version])

    def get_include_traversal_variant(self):
        return self.params.include_traversal_variant


def _get_arcadia_root(params):
    arcadia_root = params.custom_source_root
    if not arcadia_root and os.path.exists(params.packages[0]):
        arcadia_root = find_root.detect_root(params.packages[0])
    if not arcadia_root:
        arcadia_root = devtools.ya.core.config.find_root()
    arcadia_root = exts.path2.abspath(arcadia_root)
    package.display.emit_message('Source root: [[imp]]{}'.format(arcadia_root))
    if not os.path.exists(arcadia_root):
        raise YaPackageException('Arcadia root {} does not exist'.format(arcadia_root))
    return arcadia_root


def validate_package_filename(package_filename):
    if not package_filename:
        return True

    stack = []
    seen = {"package_name": False, "package_version": False, "package_ext": False}

    for idx, char in enumerate(package_filename):
        if char == '{':
            stack.append(idx)
        elif char == '}':
            if not stack:
                raise YaPackageException("{0} has invalid brackets.".format(package_filename))

            start_idx = stack.pop()
            package_part = package_filename[start_idx + 1 : idx]

            if package_part not in seen:
                raise YaPackageException(
                    "--package-filename arg doesn't have an option to pass {0}. "
                    "See docs for more info.".format(package_part)
                )

            if seen[package_part]:
                raise YaPackageException(
                    "There is no need to duplicate {0} in --package-filename arg.".format(package_part)
                )

            seen[package_part] = True

    if stack:
        raise YaPackageException("{0} has invalid brackets.".format(package_filename))

    if seen["package_ext"] and not package_filename.endswith("{package_ext}"):
        raise YaPackageException("{package_ext} option must be the last part of --package-filename arg.")

    return True


@timeit
def do_package(params):
    import app_ctx  # XXX: get via args

    package.display = app_ctx.display

    stage_started("do_package")

    if params.list_codecs:
        package.display.emit_message("\n".join(package.tarball.get_codecs_list()))
        return

    if not params.packages:
        raise YaPackageException('No packages to create')

    validate_package_filename(params.package_filename)

    arcadia_root = _get_arcadia_root(params)

    if params.run_tests == test_opts.RunTestOptions.RunAllTests and params.use_distbuild:
        raise YaPackageException('Cannot use --run-all-tests with --dist')

    if getattr(params, "checkout", False):
        logger.warning("--checkout option is not supported any more")

    packages_meta_info = []

    if params.publish_to:
        if isinstance(params.publish_to, (six.string_types, six.text_type)):
            # support for older YaPackage task that passes args via stdin
            params.publish_to = {params.publish_to: ""}

        publish_to_keys = copy.copy(set(params.publish_to.keys()))
        for key in publish_to_keys:
            if key.startswith('http://') or key.startswith('https://') and params.publish_to[key]:
                # it's url, not splited publish instruction
                params.publish_to[key + '=' + params.publish_to[key]] = None
                params.publish_to.pop(key)

        if len(params.publish_to.keys()) == 1 and not list(params.publish_to.values())[0]:  # --publish-to <repo string>
            repos = list(params.publish_to.keys())[0].split(";")
            repos_iterator = itertools.chain(iter(repos), itertools.repeat(repos[-1]))

            def repos_getter(package_file_name):
                return next(repos_iterator)

        else:
            for key, value in tuple(params.publish_to.items()):
                params.publish_to[get_package_file(arcadia_root, key)] = value

            def repos_getter(package_file_name):
                return params.publish_to.get(package_file_name, "")

    else:

        def repos_getter(package_file_name):
            return ""

    # make package files path absolute
    params.packages = [get_package_file(arcadia_root, p) for p in params.packages]

    if params.dump_build_targets:
        targets = []
        for package_file in params.packages:
            package_data = load_package(get_tree_info(arcadia_root, package_file))
            package_build = package_data.get("build", {})
            if "targets" in package_build:
                targets.extend(package_build["targets"])
            else:
                for build_key in package_build:
                    if "targets" in package_build[build_key]:
                        targets.extend(package_build[build_key]["targets"])
        targets = list(set(targets))
        logger.debug("Will dump build targets %s to %s", targets, params.dump_build_targets)
        with open(params.dump_build_targets, "w") as f:
            json.dump(targets, f)
        return

    if params.stat_only_report_file:
        params.build_only = True

    if params.dump_inputs:
        do_dump_input(params, arcadia_root, params.dump_inputs)
        return

    if not params.yt_store_wt:
        package.display.emit_message('[[warn]]Run in YT heater mode. No real package will be created')
        params.build_only = True

    if not params.build_only and params.yt_replace_result:
        raise YaPackageException("--yt-replace-result option is allowed with --build-only or --no-yt-write-through")

    for package_file in params.packages:
        logger.debug("Creating package: %s", package_file)
        package_params = copy.copy(params)
        package_params.publish_to = [_f for _f in repos_getter(package_file).split(";") if _f]
        if package_params.publish_to:
            logger.debug("Will publish %s to: %s", package_file, package_params.publish_to)

        def build_package(params, output_root):
            # XXX move to postprocess
            if params.change_log:
                params.change_log = six.ensure_str(params.change_log)

            try:
                package_context = PackageContext(arcadia_root, package_file, params)
            except Exception as e:
                logger.debug(traceback.format_exc())
                package.display.emit_message('[[bad]]{}[[rst]]'.format(e))
                raise YaPackageException("Failed to load package: {}".format(package_file)) from e

            try:
                logger.debug("Creating package: %s", locals())
                package.display.emit_message('Creating package: [[imp]]{}'.format(package_file))
                package.display.emit_message('Package root: [[imp]]{}'.format(package_context.package_root))
                package.display.emit_message('Package name: [[imp]]{}'.format(package_context.package_name))
                package.display.emit_message('Package branch: [[imp]]{}'.format(package_context.branch))

                builds = {}

                if 'build' in package_context.parsed_package:
                    if "targets" in package_context.parsed_package["build"]:
                        build_info = package_context.parsed_package["build"]
                        build_info["build_key"] = DEFAULT_BUILD_KEY
                        build_info["output_root"] = output_root
                        builds[DEFAULT_BUILD_KEY] = build_info
                    else:
                        for build_key in package_context.parsed_package["build"]:
                            build_info = package_context.parsed_package["build"][build_key]
                            build_info["build_key"] = build_key
                            build_info["output_root"] = os.path.join(output_root, build_key)
                            builds[build_key] = build_info

                    for build_info in builds.values():
                        if params.build_debian_scripts:
                            if 'targets' not in build_info:
                                build_info['targets'] = []
                            # dirty hack, fix it
                            build_info['targets'].append(os.path.relpath(os.path.dirname(package_file), arcadia_root))
                        if build_info.get("targets"):
                            stage_started("build_targets")
                            _do_build(
                                build_info,
                                params,
                                arcadia_root,
                                app_ctx,
                                package_context.parsed_package,
                                package_context.formatters,
                            )
                            stage_finished("build_targets")

                if not params.build_only:
                    stage_started("create_package")
                    name, version, path, debug_path = create_package(package_context, output_root, builds)
                    stage_finished("create_package")

                    if params.format == const.PackageFormat.DEBIAN and not params.store_debian:
                        return True

                    package_context.update_context(
                        {
                            'name': name,
                            'version': version,
                            'path': os.path.abspath(path),
                            'debug_path': debug_path and os.path.abspath(debug_path),
                        }
                    )
                    if params.upload:
                        if params.mds:
                            if not params.mds_token:
                                params.mds_token = mds_uploader.get_mds_token(params)
                            package_resource_url, platform_run_resource_url = upload_package_to_mds(
                                path, package_file, params
                            )
                            package_context.set_context("package_resource_url", package_resource_url)
                            if platform_run_resource_url:
                                package_context.set_context("platform_run_resource_url", platform_run_resource_url)
                        else:
                            package_resource_id, platform_run_resource_id = upload_package(path, package_file, params)
                            package_context.set_context("package_resource_id", package_resource_id)
                            if platform_run_resource_id:
                                package_context.set_context("platform_run_resource_id", platform_run_resource_id)
                    if params.store_debian:
                        packages_meta_info.append(package_context.context)
            except (YaPackageBuildException, YaPackageTestException):
                logger.info("Build or test failed, stop")
                raise
            except Exception as e:
                logger.info("Exception %s while build", e)
                logger.debug("Traceback: ", exc_info=True)
                package.display.emit_message(f'[[bad]]{e}[[rst]]')
                raise YaPackageException(f"Packaging {package_file} failed") from e
            finally:
                if not params.cleanup and os.path.exists(output_root):
                    build_temp = f"build.{package_context.package_name}.{random.random()}"
                    package.fs_util.copy_tree(output_root, build_temp, symlinks=True)
                    package.display.emit_message(f'Build temp directory: [[imp]]{build_temp}')

        with exts.tmp.temp_dir() as output_root:
            try:
                is_package_skipped = build_package(package_params, output_root)
            except YaPackageTestException:
                return devtools.ya.core.error.ExitCodes.TEST_FAILED
            except YaPackageBuildException as e:
                return e.original_error_code
            finally:
                if params.output_root:
                    package_path = os.path.relpath(os.path.abspath(package_file), os.path.abspath(arcadia_root))
                    exts.fs.hardlink_tree(
                        output_root,
                        os.path.join(params.output_root, package_path),
                        hardlink_function=package.fs_util.hardlink_or_copy,
                        mkdir_function=exts.fs.ensure_dir,
                    )

        if is_package_skipped:
            continue

    if packages_meta_info:
        with open('packages.json', 'w') as afile:
            json.dump(packages_meta_info, afile, indent=4)

    stage_finished("do_package")


def upload_package(built_package_file, package_json, opts):
    built_package_file_name = os.path.basename(built_package_file)
    fix_logging()
    platform_run_resource_id = None

    package_attrs = opts.resource_attrs
    package_attrs.update(
        {
            'build_type': opts.build_type,
        }
    )

    logging.info("Uploading %s", built_package_file_name)
    logging.debug("Uploading file full path: %s", built_package_file)

    package_resource_id = uploader.do(
        [built_package_file],
        resource_type=opts.resource_type,
        resource_description=package_json,
        resource_owner=opts.resource_owner,
        resource_attrs=package_attrs,
        ttl=opts.ttl,
        sandbox_url=opts.sandbox_url,
        sandbox_token=opts.sandbox_oauth_token,
        transport=opts.transport,
        ssh_keys=opts.ssh_keys,
        ssh_user=opts.username,
    )
    logger.info(
        "Uploaded package %s: https://sandbox.yandex-team.ru/resource/%s/view",
        built_package_file_name,
        package_resource_id,
    )

    platform_run_path = os.path.join(os.path.dirname(package_json), "platform.run")
    if os.path.exists(platform_run_path):
        platform_run_resource_id = uploader.do(
            [platform_run_path],
            resource_type="PLATFORM_RUN",
            resource_description="platform.run for {}({})".format(package_resource_id, package_json),
            resource_owner=opts.resource_owner,
            resource_attrs=package_attrs,
            ttl=opts.ttl,
            sandbox_url=opts.sandbox_url,
            sandbox_token=opts.sandbox_oauth_token,
            transport=opts.transport,
            ssh_keys=opts.ssh_keys,
            ssh_user=opts.username,
        )
        logger.info(
            "Uploaded platform.run for package %s with resource id %s: https://sandbox.yandex-team.ru/resource/%s/view",
            built_package_file_name,
            package_resource_id,
            platform_run_resource_id,
        )

    return package_resource_id, platform_run_resource_id


def upload_package_to_mds(built_package_file, package_json, opts):
    platform_run_resource_url = None
    built_package_file_name = os.path.basename(built_package_file)

    logger.info("Uploading %s to MDS", built_package_file_name)
    logging.debug("Uploading file full path: %s", built_package_file)

    package_resource_key = mds_uploader.do(
        [built_package_file],
        ttl=opts.ttl,
        mds_host=opts.mds_host,
        mds_port=opts.mds_port,
        mds_namespace=opts.mds_namespace,
        mds_token=opts.mds_token,
    )
    package_resource_url = "https://{}/get-{}/{}".format(opts.mds_host, opts.mds_namespace, package_resource_key)
    logger.info("Uploaded package %s: %s", built_package_file_name, package_resource_url)

    platform_run_path = os.path.join(os.path.dirname(package_json), "platform.run")
    if os.path.exists(platform_run_path):
        platform_run_resource_key = mds_uploader.do(
            [platform_run_path],
            ttl=opts.ttl,
            mds_host=opts.mds_host,
            mds_port=opts.mds_port,
            mds_namespace=opts.mds_namespace,
            mds_token=opts.mds_token,
        )
        platform_run_resource_url = "https://{}/get-{}/{}".format(
            opts.mds_host, opts.mds_namespace, platform_run_resource_key
        )
        logger.info("Uploaded platform.run for package %s: %s", built_package_file_name, platform_run_resource_url)

    return package_resource_url, platform_run_resource_url


def do_dump_input(params, arcadia_root, output):
    def safe_format(string, formatter):
        """
        This formatter will format string using only fields given in formatter.
        Fields not presented in formatter dict will be skipped.
        """
        for key, val in formatter.items():
            if isinstance(val, dict):
                for subkey, subval in val.items():
                    pattern = f"{{{key}[{subkey}]}}"
                    if pattern in string:
                        string = string.replace(pattern, str(subval))
            else:
                key = "{%s}" % key
                if key in string:
                    string = string.replace(key, str(val))
        return string

    result = {}
    for package_file in params.packages:
        if not package_file.startswith(arcadia_root):
            arcadia_root = find_root.detect_root(package_file)
            if not arcadia_root:
                arcadia_root = exts.path2.abspath(devtools.ya.core.config.find_root())
            logger.info("Find new arcadia root %s", arcadia_root)

        package_context = PackageContext(arcadia_root, package_file, params)

        build_section = collections.defaultdict(set)
        arcadia_section = set()
        sandbox_section = set()
        outputs_section = []
        formatters = package_context.formatters

        package_build = package_context.parsed_package.get("build", {})
        if "targets" in package_build:
            for build_info in _do_dump_input_build(package_build, params):
                build_section[safe_format(build_info[0], formatters)].add(safe_format(build_info[1], formatters))
        else:
            for build_key in package_build:
                if "targets" in package_build[build_key]:
                    for build_info in _do_dump_input_build(package_build[build_key], params):
                        build_section[safe_format(build_info[0], formatters)].add(
                            safe_format(build_info[1], formatters)
                        )

        # TODO: move this section getters to visitor
        for data in package_context.parsed_package.get("data", []):
            if data.get("source", {}).get("type") == 'BUILD_OUTPUT':
                item = data.get("source", {})
                if "path" in item:
                    item["path"] = safe_format(item["path"], formatters)
                outputs_section.append(item)
            if data.get("source", {}).get("type") == 'ARCADIA':
                for path in _do_dump_input_arcadia(
                    arcadia_root, None, data["source"].get("path"), data["source"].get("files")
                ):
                    arcadia_section.add(safe_format(path, formatters))
            if data.get("source", {}).get("type") == 'RELATIVE':
                for path in _do_dump_input_arcadia(
                    arcadia_root, package_context.package_root, data["source"].get("path"), data["source"].get("files")
                ):
                    arcadia_section.add(safe_format(path, formatters))
            if data.get("source", {}).get("type") == 'SANDBOX_RESOURCE':
                sandbox_id = data["source"].get("id")
                if id:
                    sandbox_section.add(sandbox_id)

        include_section = package_context.tree_info.get_recursive_includes(arcadia_root)

        result[package_context.package_path] = {
            'build': {k: list(sorted(v)) for k, v in build_section.items()},
            'arcadia': list(sorted(arcadia_section)),
            'sandbox': list(sorted(sandbox_section)),
            'include': list(sorted(include_section)),
            'build_outputs': outputs_section,
        }

    with open(output, 'w') as out:
        out.write(json.dumps(result, sort_keys=True, indent=2))


def _do_dump_input_build(build_info, params):
    targets = build_info.get("targets")
    if not targets:
        return
    build_type = build_info.get("build_type", params.build_type)
    flags = {item["name"]: item.get("value") for item in build_info.get("flags", [])}
    for platform in build_info.get("target-platforms", []) or ["DEFAULT-LINUX-X86_64"]:
        canonized_platform = bg.mine_platform_name(platform)
        platform_key = json.dumps({f"{canonized_platform},{build_type}": flags}, sort_keys=True)
        for t in targets:
            yield platform_key, t


def _do_dump_input_arcadia(arcadia_root, root, path, files):
    def list_dir(d):
        for r, _, files_list in os.walk(d):
            for f in files_list:
                yield os.path.join(r, f)[len(d) + 1 :]

    if not path:
        return
    if root:
        path = os.path.join(root, path)
    abs_path = os.path.abspath(os.path.join(arcadia_root, path))
    if os.path.isfile(abs_path):
        yield path
    if os.path.isdir(abs_path):
        if files:
            for f, _ in package.source.filter_files(abs_path, files):
                yield os.path.normpath(os.path.join(path, f))
        else:
            for f in list_dir(abs_path):
                yield os.path.normpath(os.path.join(path, f))
