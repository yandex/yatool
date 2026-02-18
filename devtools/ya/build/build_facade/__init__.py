import logging
import os

import exts.yjson as json
from exts.strtobool import strtobool
from exts.tmp import temp_file

import devtools.ya.core.config
import devtools.ya.core.yarg

import devtools.ya.build.evlog
import devtools.ya.build.gen_plan
import devtools.ya.build.genconf
import devtools.ya.build.targets
import devtools.ya.build.ymake2

logger = logging.getLogger(__name__)


def _gen(
    custom_build_directory,
    build_type,
    build_targets,
    debug_options,
    flags=None,
    warn_mode=None,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
    grab_stderr=True,
    evlog=False,
    find_path_from=None,
    find_path_to=None,
    modules_info_file=None,
    modules_info_filter=None,
    lic_link_type=None,
    lic_custom_tags=[],
    managed_dep_tree=None,
    classpaths=None,
    dump_file=None,
    custom_conf_dir=None,
    patch_path=None,
):
    generation_conf = gen_conf(
        build_root=custom_build_directory,
        build_type=build_type,
        build_targets=build_targets,
        flags=flags,
        host_platform=host_platform,
        target_platforms=target_platforms,
        custom_conf_dir=custom_conf_dir,
    )
    res, evlog_dump = devtools.ya.build.ymake2.ymake_dump(
        custom_build_directory=custom_build_directory,
        build_type=build_type,
        abs_targets=build_targets,
        debug_options=debug_options,
        warn_mode=warn_mode,
        flags=flags,
        ymake_bin=ymake_bin,
        grab_stderr=grab_stderr,
        custom_conf=generation_conf,
        evlog=evlog,
        find_path_from=find_path_from,
        find_path_to=find_path_to,
        modules_info_file=modules_info_file,
        modules_info_filter=modules_info_filter,
        lic_link_type=lic_link_type,
        lic_custom_tags=lic_custom_tags,
        managed_dep_tree=managed_dep_tree,
        classpaths=classpaths,
        dump_file=dump_file,
        patch_path=patch_path,
        disable_customization=strtobool(flags.get('DISABLE_YMAKE_CONF_CUSTOMIZATION', 'no')),
    )
    return (res, evlog_dump) if evlog else res


def gen_conf(
    build_root,
    build_type,
    build_targets=None,
    flags={},
    host_platform=None,
    target_platforms=None,
    arc_root=None,
    custom_conf_dir=None,
    **kwargs
):
    if not host_platform:
        host_platform = devtools.ya.build.genconf.host_platform_name()
    else:
        host_platform = devtools.ya.build.genconf.mine_platform_name(host_platform)
    if target_platforms:
        if len(target_platforms) > 1:
            logger.error('Multiple target platforms are not supported by this code for now')
            raise NotImplementedError
        toolchain_params = devtools.ya.build.genconf.gen_cross_tc(
            host_platform, devtools.ya.build.genconf.mine_platform_name(target_platforms[0]['platform_name'])
        )
        flags = flags.copy()
        flags.update(target_platforms[0].get('flags', {}))
    else:
        toolchain_params = devtools.ya.build.genconf.gen_tc(host_platform)
    logger.debug('Toolchain params for config generation: %s', json.dumps(toolchain_params, indent=2))
    if not arc_root:
        arc_root = devtools.ya.core.config.find_root_from(build_targets)
    generation_conf, _ = devtools.ya.build.genconf.gen_conf(
        arc_dir=arc_root,
        conf_dir=custom_conf_dir or devtools.ya.build.genconf.detect_conf_root(arc_root, build_root),
        build_type=build_type,
        use_local_conf=True,
        local_conf_path=None,
        extra_flags=flags,
        tool_chain=toolchain_params,
    )
    return generation_conf


def gen_dependencies(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['z'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_graph(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['g'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_modules(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['g', 'm'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_module_info(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
    modules_info_file=None,
    modules_info_filter=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['h'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
        modules_info_file=modules_info_file,
        modules_info_filter=modules_info_filter,
    )


def gen_licenses_list(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
    lic_link_type=None,
    lic_custom_tags=[],
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
        lic_link_type=lic_link_type,
        lic_custom_tags=lic_custom_tags,
    )


def gen_forced_deps_list(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_srcdeps(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['src-deps'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_json_graph(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
    custom_conf_dir=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['g', 'J'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
        custom_conf_dir=custom_conf_dir,
    )


def gen_include_targets(
    build_root=None,
    build_type='release',
    build_targets=None,
    debug_options=None,
    flags=None,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    debug_options = debug_options or []
    flags = flags or {}
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['B', 'x'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_managed_dep_tree(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        flags=flags,
        managed_dep_tree=build_targets,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
        evlog=True,
    )


def gen_targets_classpath(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        flags=flags,
        classpaths=build_targets,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
        evlog=True,
    )


def gen_relation(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    find_path_from,
    find_path_to,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    arc_root = devtools.ya.core.config.find_root_from(build_targets)

    def normalize_targets(targets):
        prefix_targets = [x for x in targets if x[0] == '$']
        path_targets = [x for x in targets if x[0] != '$']
        if path_targets:
            info = devtools.ya.build.targets.resolve(arc_root, path_targets)
            path_targets = [os.path.relpath(x, info.root) for x in info.targets]
        return prefix_targets + path_targets

    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        flags=flags,
        find_path_from=normalize_targets(find_path_from),
        find_path_to=normalize_targets(find_path_to),
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_all_loops(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        warn_mode=['allloops'],
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_dir_loops(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        warn_mode=['dirloops'],
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_build_targets(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['a'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_owners(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['O'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )


def gen_filelist(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
    custom_conf_dir=None,
    patch_path=None,
):
    return _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=['g', 'f'] + debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
        custom_conf_dir=custom_conf_dir,
        patch_path=patch_path,
    )


def gen_plan_options(
    arc_root,
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    no_ymake_resource=False,
    custom_conf=None,
    strict_inputs=False,
    dump_inputs_map=False,
    vcs_file=None,
):
    return devtools.ya.core.yarg.Params(
        arc_root=arc_root,
        debug_options=debug_options,
        mode='gen_graph',
        build_type=build_type,
        custom_build_directory=build_root,
        abs_targets=build_targets,
        grab_stderr=True,
        continue_on_fail=True,
        dump_graph='json',
        clear_build=False,
        flags=flags,
        ymake_bin=ymake_bin,
        no_ymake_resource=no_ymake_resource,
        custom_conf=custom_conf,
        strict_inputs=strict_inputs,
        dump_inputs_map=dump_inputs_map,
        vcs_file=vcs_file,
    )


def gen_plan(
    arc_root,
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    no_ymake_resource=False,
    vcs_file=None,
):
    return devtools.ya.build.gen_plan.gen_plan(
        gen_plan_options(
            arc_root=arc_root,
            build_root=build_root,
            build_type=build_type,
            build_targets=build_targets,
            debug_options=debug_options,
            flags=flags,
            ymake_bin=ymake_bin,
            no_ymake_resource=no_ymake_resource,
            vcs_file=vcs_file,
        )
    )


def gen_java_projects(build_root, build_targets, flags, ymake_bin=None):
    f = 'java_targets_paths.lst'
    res, _ = devtools.ya.build.ymake2.ymake_dump(
        custom_build_directory=build_root,
        abs_targets=build_targets,
        dump_info=['java_projects:{}'.format(f)],
        flags=flags,
        ymake_bin=ymake_bin,
    )
    return res, f


def gen_test_dart(build_root, **kwargs):
    with temp_file() as test_dart:
        kwargs.update(
            dict(
                custom_build_directory=build_root,
                run_tests=3,
                dump_tests=test_dart,
                debug_options=['x'] + kwargs.get('debug_options', []),
                grab_stdout=True,
                grab_stderr=True,
            )
        )

        res, _ = devtools.ya.build.ymake2.ymake_dump(**kwargs)

        def read_if_exists(path):
            if os.path.exists(path):
                with open(path) as f:
                    return f.read()
            else:
                return ''

        test_dart_content = read_if_exists(test_dart)

    return res, test_dart_content


def _extract_uids(build_plan):
    target_uid_map = {}
    for node in build_plan['graph']:
        if node['uid'] in build_plan['result']:
            for target in node['outputs']:
                target_uid_map[target.replace('$(BUILD_ROOT)/', '')] = node['uid']
    return target_uid_map


def gen_uids(
    arc_root,
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
):
    generation_conf = gen_conf(
        arc_root=arc_root,
        build_root=build_root,
        build_type=build_type,
        build_targets=build_targets,
        flags=flags,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )

    return _extract_uids(
        devtools.ya.build.gen_plan.gen_plan(
            gen_plan_options(
                arc_root=arc_root,
                build_root=build_root,
                build_type=build_type,
                build_targets=build_targets,
                debug_options=debug_options,
                flags=flags,
                ymake_bin=ymake_bin,
                custom_conf=generation_conf,
            )
        )
    )
