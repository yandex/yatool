"""Configuration policy shared by regular and native graph consumers.

Preparation does not launch ymake or inspect its resulting graph.
"""

import copy
from enum import Enum
import logging
import os

import exts.fs
import exts.hashing as hashing
from exts.strtobool import strtobool
import devtools.ya.core.yarg
import yalibrary.platform_matcher as pm
from devtools.ya.build import genconf as bg, gen_plan, node_checks
from devtools.ya.build.build_graph_cache import layout as bg_cache_layout, target_tc as bg_cache_target_tc
from devtools.ya.build.ymake2.consts import YmakeEvents
from devtools.ya.yalibrary.yandex.distbuild import distbs_consts

logger = logging.getLogger(__name__)

HOST_TOOL_FLAGS = {
    'NO_DEBUGINFO': 'yes',
    'TOOL_BUILD_MODE': 'yes',
    'CLANG_COVERAGE': 'no',
    'TRAVERSE_RECURSE': 'no',
    'CONSISTENT_DEBUG': 'yes',
    'TIDY': 'no',
}


class ConfigurationKind(Enum):
    TARGET = 0
    TOOLS = 1
    GLOBAL_TOOLS = 2


def extend_build_type(build_type):
    if build_type.startswith('dist-'):
        return build_type
    return 'dist-' + build_type


def should_run_tests(opts, target_tc):
    return not node_checks.is_tools_tc(target_tc) and (
        opts.run_tests or target_tc.get('run_tests') or opts.list_tests or opts.canonize_tests
    )


def iter_target_flags(opts):
    for opt_val, flag in [
        (opts.sanitize, 'SANITIZER_TYPE'),
        (opts.sanitize_coverage, 'SANITIZE_COVERAGE'),
    ]:
        if not opt_val:
            opt_val = opts.flags.get(flag, None)
            if opt_val and flag in opts.flags:
                del opts.flags[flag]

        if opt_val:
            yield flag, opt_val

    if opts.yndexing:
        yield 'CODENAVIGATION', 'yes'

    if opts.sanitizer_flags:
        yield 'SANITIZER_CFLAGS', ' '.join(opts.sanitizer_flags)

    if opts.lto:
        yield 'USE_LTO', 'yes'

    if opts.thinlto:
        yield 'USE_THINLTO', 'yes'

    if opts.musl:
        yield 'MUSL', 'yes'

    if opts.use_afl:
        yield 'USE_AFL', 'yes'

    if opts.pgo_add:
        yield 'PGO_ADD', 'yes'

    if opts.pgo_path:
        yield 'PGO_PATH', '$(PGO_PATH)'

    if opts.javac_flags:
        yield 'JAVAC_OPTS', ' '.join(['-{} {}'.format(k, v or '') for k, v in opts.javac_flags.items()])

    if opts.hardening:
        yield 'HARDENING', 'yes'

    if opts.sandboxing:
        yield 'SANDBOXING', 'yes'

    if opts.race:
        yield 'RACE', 'yes'

    if opts.cuda_platform == 'disabled':
        yield 'CUDA_DISABLED', 'yes'
        yield 'HAVE_CUDA', 'no'
    if opts.cuda_platform == 'required':
        yield 'CUDA_REQUIRED', 'yes'

    if opts.preset_mapsmobi:
        yield 'MAPSMOBI_BUILD_TARGET', 'yes'

    if opts.preset_with_credits:
        yield 'WITH_CREDITS', 'yes'


def execution_host_platform(opts) -> str:
    """Resolve the host without changing target normalization or toolchain state."""
    if opts.host_platform:
        return bg.mine_platform_name(opts.host_platform)
    if opts.use_distbuild:
        return bg.mine_platform_name('default-linux-x86_64')
    return bg.host_platform_name()


def resolve_toolchains(opts, *, report_warning=None):
    host = opts.host_platform
    target_platforms = opts.target_platforms

    if host:
        host = bg.mine_platform_name(host)

    if target_platforms:
        target_platforms = [x.copy() for x in target_platforms]
        for platform in target_platforms:
            platform['platform_name'] = bg_cache_target_tc.normalize_target_platform_name(
                platform.get('platform_name'),
                host or bg.host_platform_name(),
            )

    if opts.use_distbuild:
        if not host:
            host = execution_host_platform(opts)
        if not target_platforms:
            target_platforms = [{'platform_name': bg.host_platform_name()}]
    else:
        if (
            pm.is_darwin_rosetta()
            and not opts.hide_arm64_host_warning
            and not host
            and not target_platforms
            and report_warning is not None
        ):
            try:
                report_warning(
                    "Current target architecture is x86_64. Specify \"--target-platform\" flag to build for native arm64."
                )
            except Exception as e:
                logger.error("Can't print arm64 warning message: {}".format(e))
        if not host:
            host = execution_host_platform(opts)
        if not target_platforms:
            target_platforms = [{'platform_name': host}]

    target_flags = dict(iter_target_flags(opts))

    if target_flags:
        for t in target_platforms:
            t['flags'] = dict(target_flags, **t.get('flags', {}))
    # done propagate

    # add salt for target platforms in sandboxing mode
    sandboxing_salt = 'sandboxing'
    for t in target_platforms:
        if t.get('flags', {}).get('SANDBOXING') == 'yes':
            t['flags']['FAKEID'] = t['flags'].get('FAKEID', '') + sandboxing_salt

    if len(target_platforms) > 1 and opts.canonize_tests:
        raise devtools.ya.core.yarg.ArgsBindingException(
            'Canonization is not supported for more than one target platform'
        )

    host_tc = bg.gen_tc(
        host, opts.c_compiler, opts.cxx_compiler, opts.flags.get('IGNORE_MISMATCHED_XCODE_VERSION') == 'yes'
    )
    host_tc['build_type'] = opts.host_build_type
    host_tc['flags'] = dict(HOST_TOOL_FLAGS)
    if opts.host_platform_flags:
        host_tc['flags'].update(opts.host_platform_flags)
    if opts.preset_mapsmobi:
        host_tc['flags']['MAPSMOBI_BUILD_HOST'] = 'yes'
    if opts.sandboxing:
        host_tc['flags']['SANDBOXING'] = 'yes'
    if host_tc['flags'].get('SANDBOXING') == 'yes':
        host_tc['flags']['FAKEID'] = host_tc['flags'].get('FAKEID', '') + sandboxing_salt

    target_tcs = [bg_cache_target_tc.resolve_target_tc(host, p, opts) for p in target_platforms]
    return host, host_tc, target_tcs


def configuration_variants(opts, target_tc, flags, graph_kind):
    if target_tc['platform']['target']['os'] == 'WIN':
        return ('nopic',)
    if opts.force_pic or bg_cache_layout.is_pic_only(flags, target_tc):
        return ('pic',)
    if graph_kind == ConfigurationKind.TOOLS:
        return ('pic',)
    if graph_kind == ConfigurationKind.GLOBAL_TOOLS:
        return ('nopic',)
    return ('nopic', 'pic')


def internal_transition_options(kind, variant, platform_id, variants):
    if kind != ConfigurationKind.TARGET:
        return {'platform_id': platform_id} if variant == 'pic' else {}
    result = {'platform_id': platform_id, 'transition_source': variant}
    if variant == 'nopic':
        result['report_pic_nopic'] = 'pic' in variants
    elif 'nopic' in variants:
        result.update(targets_from_evlog=True, dont_check_transitive_requirements=True)
    return result


def configuration_flags(opts, tc, extra_flags):
    flags = {}

    flags.update(extra_flags)
    flags['IS_CROSS_SANITIZE'] = 'yes'
    flags.update(tc.get('params', {}).get('extra_flags', {}))
    flags.update(tc.get('flags', {}))
    flags[tc['name'].upper()] = 'yes'
    flags['YA'] = 'yes'

    if not node_checks.is_tools_tc(tc) and (should_run_tests(opts, tc) or opts.force_build_depends):
        flags['TRAVERSE_RECURSE_FOR_TESTS'] = 'yes'
        flags['TRAVERSE_DEPENDS'] = 'yes'
    if opts.ignore_recurses or tc.get('ignore_recurses'):
        flags['TRAVERSE_RECURSE'] = 'no'
        flags['TRAVERSE_RECURSE_FOR_TESTS'] = 'no'

    return flags


class ConfigurationPreparation:
    def __init__(self, opts, source_root, conf_dir, ymake_identity, changelist_generator=None):
        self._opts = opts
        self._src_dir = source_root
        self._conf_dir = conf_dir
        self._ymake_bin = ymake_identity
        self._heater = opts.build_graph_cache_heater
        self._allow_changelist = True
        self._cl_generator = changelist_generator

    def prepare(
        self,
        extra_flags,
        tc,
        test_dart,
        java_dart,
        make_files_dart,
        ev_listener,
        abs_targets,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        cache_dir=None,
        change_list=None,
        extra_conf=None,
        no_caches_on_retry=False,
        no_ymake_retry=False,
        cache_subdir=None,
    ):
        flags = configuration_flags(self._opts, tc, extra_flags)

        build_type = gen_plan.real_build_type(tc, self._opts)
        conf_path, _ = bg.gen_conf(
            self._src_dir,
            self._conf_dir,
            extend_build_type(build_type),
            self._opts.use_local_conf,
            self._opts.local_conf_path,
            flags,
            tc,
            not self._opts.use_distbuild,
            conf_debug=self._opts.conf_debug_options,
            debug_id=debug_id,
            extra_conf=extra_conf,
        )

        uniq_id = hashing.md5_value(str([2, self._ymake_bin]))

        ya_cache_dir = cache_dir or (
            os.path.join(os.path.dirname(conf_path), cache_subdir)
            if cache_subdir
            else os.path.dirname(conf_path) + '_' + uniq_id
        )

        if cache_subdir:
            os.makedirs(ya_cache_dir, exist_ok=True)

        if not self._heater and cache_dir and not os.path.isfile(os.path.join(ya_cache_dir, 'ymake.cache')):
            logger.debug("Cannot find ymake.cache in %s, ignore if there is no target built", ya_cache_dir)

        o = dict(
            custom_build_directory=ya_cache_dir,
            build_type=build_type,
            abs_targets=abs_targets,
            debug_options=self._opts.debug_options,
            custom_conf=conf_path,
            continue_on_fail=self._opts.continue_on_fail,
            ymake_bin=self._opts.ymake_bin,
            warn_mode=self._opts.warn_mode,
            build_depends=(
                not node_checks.is_tools_tc(tc) and (should_run_tests(self._opts, tc) or self._opts.force_build_depends)
            ),
            checkout_data_by_ya=getattr(self._opts, "checkout_data_by_ya", False),
            strict_inputs=self._need_strict_inputs(flags),
            dump_inputs_map=self._is_inputs_map_required(),
            ev_listener=self._get_event_listener_debug_id_wrapper(ev_listener, debug_id, tc),
            enabled_events=enabled_events,
            no_caches_on_retry=no_caches_on_retry,
            no_ymake_retry=no_ymake_retry or self._opts.ymake_multiconfig,
            fail_on_no_configure_cache=self._opts.fail_on_no_configure_cache,
            disable_customization=strtobool(flags.get('DISABLE_YMAKE_CONF_CUSTOMIZATION', 'no')),
            parallel_rendering=self._opts.ymake_parallel_rendering,
            use_subinterpreters=self._opts.ymake_use_subinterpreters,
        )

        if test_dart:
            o["dump_tests"] = test_dart

        if java_dart:
            o['dump_java'] = java_dart

        if make_files_dart:
            o['dump_make_files'] = make_files_dart

        if change_list is None:
            if getattr(self._opts, "build_graph_cache_force_local_cl", False) and self._allow_changelist:
                if self._cl_generator is not None:
                    o['changelist_generator'] = (
                        self._cl_generator
                    )  # TODO: think of a proper way to pass it closer to ymake launch
                    o['patch_path'] = self._cl_generator.get_changelist(ya_cache_dir)
                    if o['patch_path'] is not None and 'completely-trust-fs-cache' not in o['debug_options']:
                        o['debug_options'] = o['debug_options'] + ['completely-trust-fs-cache']
        else:
            o['patch_path'] = change_list

        if self._opts.dump_file_path:
            assert debug_id
            exts.fs.ensure_dir(self._opts.dump_file_path)
            # Truncate filename to be sure it can be created on the FS
            if len(debug_id) > 150:
                suffix = "-{md5}".format(md5=hashing.md5_value(debug_id))
                filename = debug_id[: 150 - len(suffix)] + suffix + '.txt'
            else:
                filename = debug_id + '.txt'
            o['dump_file'] = os.path.join(self._opts.dump_file_path, filename)

        if self._opts.compress_ymake_output:
            assert self._opts.compress_ymake_output_codec, "Compress codec must be defined"
            o['compress_ymake_output_codec'] = self._opts.compress_ymake_output_codec

        return o

    def _need_strict_inputs(self, flags):
        return self._opts.strict_inputs or flags.get('SANDBOXING') == 'yes'

    def _is_inputs_map_required(self):
        return (
            self._opts.use_distbuild and self._opts.repository_type == distbs_consts.DistbuildRepoType.TARED
        ) or self._opts.run_tagged_tests_on_sandbox

    def _event_listener_invalid_recurses_wrapper(self, ev_listener, tc):
        if self._opts.dump_platform_to_evlog:

            def enrich_invalid_recurses_listener(event):
                if event['_typename'] in ('NEvent.TInvalidRecurse', 'NEvent.TDisplayMessage', 'NEvent.TFailOnRecurse'):
                    event['Platform'] = pm.stringize_toolchain(tc)
                ev_listener(event)

            return enrich_invalid_recurses_listener
        else:
            return ev_listener

    def _get_event_listener_debug_id_wrapper(self, ev_listener, debug_id, tc):
        event_listener_invalid_recurses_wrapper_func = self._event_listener_invalid_recurses_wrapper(ev_listener, tc)

        def event_listener_debug_id_wrapper(event):
            if (
                event['_typename'] in ("NEvent.TStageStarted", "NEvent.TStageFinished")
                and event["StageName"] == "ymake run"
            ):
                event["debug_id"] = debug_id
            event_listener_invalid_recurses_wrapper_func(event)

        return event_listener_debug_id_wrapper


def configuration_base_flags(opts, kind):
    if kind in (ConfigurationKind.TOOLS, ConfigurationKind.GLOBAL_TOOLS):
        flags = opts.host_flags.copy()
        flags.update(opts.host_platform_flags)
        flags['IS_CROSS_TOOLS'] = 'yes'
        return flags
    return opts.flags.copy()


def configuration_metadata(opts, tc, flags, extra_tag=None, pic_only=False):
    tags, platform, platform_id = gen_plan.prepare_tags(tc, flags, opts)
    result = {'tags': tags + ([extra_tag] if extra_tag else []), 'platform': platform}
    if platform_id:
        if extra_tag and (extra_tag != 'pic' or not pic_only):
            platform_id = f'{platform_id}-{extra_tag}'
        result['platform_id'] = platform_id
    if node_checks.is_tools_tc(tc) and opts.host_platform_id:
        result['platform_id'] = opts.host_platform_id
    sandboxing = tc.get('flags', {}).get('SANDBOXING', 'no')
    result['sandboxing'] = bool(strtobool(sandboxing)) if node_checks.is_tools_tc(tc) else sandboxing == 'yes'
    return result


def runtime_resources(resolver):
    return [resolver.resolve(tool, pattern) for tool, pattern in (('python', 'PYTHON'), ('python3', 'YMAKE_PYTHON3'))]


def test_runtime_resources(resolver, opts, target_tcs=(), *, suite_tools=()):
    resources = [resolver.resolve(tool, tool.upper()) for tool in suite_tools]
    optional_tools = ['gdb']
    if opts.dlv:
        optional_tools.append('dlv')
    if any('valgrind' in gen_plan.real_build_type(tc, opts) for tc in target_tcs):
        optional_tools.append('valgrind')
    for tool in optional_tools:
        try:
            resources.append(resolver.resolve(tool, tool.upper()))
        except Exception as error:
            logger.debug('%s will not be available for tests: %s', tool, error)
    return resources


def variant_flags(flags, variant):
    flags = copy.deepcopy(flags)
    if variant == 'nopic':
        flags['FORCE_NO_PIC'] = 'yes'
    return flags
