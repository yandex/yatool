import json
import logging
import os
import shutil
import subprocess

import pygtrie

from packaging import version
from build.ymake2 import run_ymake
import build.graph as bg
import core.yarg
import build.build_opts as bo
import yalibrary.tools
from build import build_facade, ya_make
import yalibrary.platform_matcher as pm
import time

logger = logging.getLogger(__name__)

IDE_YMAKE_BUILD_TYPE = 'nobuild'
GRADLE_DIR = os.path.join(os.path.expanduser('~'), '.ya', 'build', 'hic_sunt_dracones')
CACHE_DIR = os.path.join(GRADLE_DIR, 'cache')
targets_to_build = set()
cached_libs = dict()
lib_versions = dict()

project_modules = set()
project_test_modules = set()

tree = pygtrie.StringTrie()


def delete_all_gradle(root, params, flag):
    if os.path.exists(root):
        os.remove(root)


def delete_useless_folders(root):
    for dirpath, dirnames, filenames in os.walk(root, topdown=False):
        for dirname in dirnames:
            full_path = os.path.join(dirpath, dirname)
            list_dir = os.listdir(full_path)
            if not list_dir or (len(list_dir) == 1 and list_dir[0] == 'build.gradle.kts'):
                shutil.rmtree(full_path)


def find_all_modules(sem_json, project_prefix):
    with open(sem_json) as f:
        templates = json.load(f)
    for node in templates['data']:
        if 'NodeType' in node and node['NodeType'] == 'Directory' and 'Tag' in node and node['Tag'] == 'StartDir':
            name = node['Name'].replace('$S/', '')
            if project_prefix in name and project_prefix != name:
                if len(list(tree.prefixes(name))) == 0:
                    project_modules.add(name)
                    tree[name] = 'foo'
                elif 'src' not in name:
                    project_test_modules.add(name)


def find_module_for_test(test_module):
    prefs = list(tree.prefixes(test_module))
    if len(prefs) > 0:
        return prefs[0].key.split(os.sep)[-1]


def bootstrap(func):
    def inner(*args, **kwargs):
        params = args[0]
        if not os.path.isdir(GRADLE_DIR):
            os.mkdir(GRADLE_DIR)

        if not os.path.isdir(CACHE_DIR):
            os.mkdir(CACHE_DIR)
        gradle_names = ('build.gradle.kts', 'settings.gradle.kts', 'build.gradle', 'settings.gradle')

        for gradle_name in gradle_names:
            go_through_gradle(os.path.join(params.arc_root, params.rel_targets[0]), delete_all_gradle,
                              params, gradle_name, 'bootstrap')
        func(*args, **kwargs)

    return inner


def resolve_transitives(line):
    def check_dep(found_lib_sep):
        lib_name = found_lib_sep[0] + ':' + found_lib_sep[1]
        lib_ver = found_lib_sep[2]

        if lib_name not in lib_versions:
            lib_versions[lib_name] = lib_ver
        elif version.parse(lib_versions[lib_name]) < version.parse(lib_ver):
            lib_versions[lib_name] = lib_ver
        lib_versions['com.squareup.okhttp3:okhttp'] = '3.14.9'

    if 'api("' in line:
        found_lib = line[line.find('api("') + 5:len(line) - 3].split(':')
        lib_name = found_lib[0] + ':' + found_lib[1]
        check_dep(found_lib)
        return '    api("' + lib_name + ':' + lib_versions[lib_name] + '")\n'
    if 'testImplementation("' in line:
        found_lib = line[line.find('testImplementation("') + 20:len(line) - 3].split(':')
        lib_name = found_lib[0] + ':' + found_lib[1]
        check_dep(found_lib)
        return '    testImplementation("' + lib_name + ':' + lib_versions[lib_name] + '")\n'
    return ''


def resolve_non_contrib_peerdir(root, params, only_build):
    bld_gradle = os.path.join(root, 'build.gradle.kts')
    with open(bld_gradle, 'r') as f:
        data = f.readlines()
    file = open(bld_gradle, 'w+')
    new_data = list()

    for line in data:
        resolved_line = resolve_transitives(line)
        if resolved_line != '':
            new_data.append(resolved_line)
            continue
        # переделать на регулярки
        if 'api(project(' in line:
            found_lib = line[line.find('api(project(') + 14:len(line) - 4].replace(':', os.sep)
            logger.info(found_lib)

            if only_build:
                new_data.append(line)
                if found_lib not in project_modules and found_lib not in project_test_modules:
                    if found_lib == 'arc/api/grpc':
                        targets_to_build.add('arc/api/proto')
                    targets_to_build.add(found_lib)
                continue

            # временный хак для arc api
            if found_lib == 'arc/api/grpc':
                build_lib = build_target('arc/api/proto', params)
                new_data.append('    api(files("' + os.path.join(CACHE_DIR, build_lib) + '"))\n')

            if found_lib not in project_modules:
                build_lib = build_target(found_lib, params)
                new_data.append('    api(files("' + os.path.join(CACHE_DIR, build_lib) + '"))\n')
            else:
                new_data.append('    api(project(":' + found_lib.split(os.sep)[-1] + '"))\n')
                new_data.append('    api(project(path=":' + found_lib.split(os.sep)[-1] + '", configuration'
                                                                                          '="testOutput"))\n')
        elif 'testImplementation(project(' in line:
            found_lib = line[line.find('testImplementation(project(') + 29:len(line) - 4].replace(':', os.sep)
            logger.info(found_lib)

            if only_build:
                new_data.append(line)
                if found_lib not in project_modules and found_lib not in project_test_modules:
                    # хак для Арка
                    if found_lib == 'arc/api/grpc':
                        targets_to_build.add('arc/api/proto')
                    targets_to_build.add(found_lib)
                continue

            if found_lib in project_test_modules:
                module_for_test = find_module_for_test(found_lib)
                new_data.append('    testImplementation(project(":' + module_for_test + '"))\n')
                new_data.append('    testImplementation(project(path=":' + module_for_test + '", configuration'
                                                                                             '="testOutput"))\n')
                continue

            if found_lib not in project_modules:
                build_lib = build_target(found_lib, params)
                logger.info('trying to get ' + found_lib)
                new_data.append('    testImplementation(files("' + os.path.join(CACHE_DIR, build_lib) + '"))\n')
            else:
                new_data.append('    testImplementation(project(":' + found_lib.split(os.sep)[-1] + '"))\n')
        else:
            new_data.append(line)

    for line in new_data:
        file.write(line)
    file.close()

    module_for_root = root.replace(params.abs_targets[0] + os.sep, '')
    if module_for_root in project_modules or root == os.path.join(params.arc_root, params.rel_targets[0],
                                                                  params.rel_targets[0]):
        shutil.move(bld_gradle,
                    os.path.join(params.arc_root, bld_gradle[len(params.arc_root) + len(params.rel_targets[0]) + 2:]))


def fix_settings_gradle(params):
    settings_gradle = os.path.join(params.abs_targets[0], 'settings.gradle.kts')
    with open(settings_gradle, 'r') as f:
        data = f.readlines()
    file = open(settings_gradle, 'w+')
    new_data = list()

    for line in data:
        if 'include(' in line:
            found_module = line[len('include("'):len(line) - 3].replace(':', os.sep)
            if found_module in project_modules:
                module = found_module.split(os.sep)[-1]
                new_data.append('include("' + module + '")\n')
        else:
            new_data.append(line)

    for line in new_data:
        file.write(line)
    file.close()


def go_through_gradle(root, func, params, gradle_name, flag):
    for item in os.listdir(root):
        abs_path = os.path.join(root, item)
        if item == gradle_name:
            logger.info('build gradle found: ' + abs_path)
            if flag == 'bootstrap':
                func(os.path.join(root, gradle_name), params, flag)
            else:
                func(root, params, flag)
        if os.path.isdir(abs_path):
            go_through_gradle(abs_path, func, params, gradle_name, flag)


def build_target(target, params):
    import app_ctx

    logger.info('trying to build ' + target)
    if target in cached_libs:
        return cached_libs[target]
    ya_make_opts = core.yarg.merge_opts(bo.ya_make_options(free_build_targets=True))
    jopts = core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra))

    jopts.bld_dir = params.bld_dir
    jopts.arc_root = params.arc_root
    jopts.bld_root = params.bld_root

    jopts.rel_targets = list()
    jopts.abs_targets = list()

    for trg in targets_to_build:
        jopts.rel_targets.append(trg)
        jopts.abs_targets.append(os.path.join(params.arc_root, trg))

    logger.info(jopts.rel_targets)
    logger.info(jopts.abs_targets)
    # потом поправить и сделать красивый лог сборки
    listeners = []
    ev_listener = ya_make.compose_listeners(*listeners)
    graph, _, _, ctx, _ = bg.build_graph_and_tests(jopts, check=True, ev_listener=ev_listener, display=app_ctx.display)
    logger.info('graph built')
    builder = ya_make.YaMake(jopts, app_ctx, graph=graph, tests=[])
    builder.go()

    def get_built_name(target_root):
        for path in os.listdir(target_root):
            if 'jar' in path and 'source' not in path:
                return path

    for trg in targets_to_build:
        target_with_root = os.path.join(params.arc_root, trg)
        built_name = get_built_name(target_with_root)
        dest_built_name = os.path.join(CACHE_DIR, built_name)
        if os.path.exists(dest_built_name):
            os.remove(dest_built_name)
        shutil.copy(os.path.join(target_with_root, built_name), dest_built_name)
        cached_libs[trg] = built_name


@bootstrap
def do_gradle(params):
    start_time = time.time()
    if pm.my_platform() == 'win32':
        logger.error("Win is not supported in ya ide gradle")
        return

    project_prefix = params.rel_targets[0]
    project_modules.add(project_prefix)
    target = params.abs_targets[0]
    flags = {'EXPORTED_BUILD_SYSTEM_SOURCE_ROOT': target,
             'EXPORTED_BUILD_SYSTEM_BUILD_ROOT': GRADLE_DIR,
             'EXPORT_GRADLE': 'yes',
             'TRAVERSE_RECURSE': 'yes',
             'TRAVERSE_RECURSE_FOR_TESTS': 'yes',
             'BUILD_LANGUAGES': 'JAVA',  # KOTLIN == JAVA
             'USE_PREBUILT_TOOLS': 'no',
             'OPENSOURCE': 'yes',
             }

    params.flags.update(flags)
    conf = build_facade.gen_conf(
        build_root=GRADLE_DIR,
        build_type=IDE_YMAKE_BUILD_TYPE,
        build_targets=params.abs_targets,
        flags=params.flags,
        ymake_bin=getattr(params, 'ymake_bin', None),
        host_platform=params.host_platform,
        target_platforms=params.target_platforms

    )

    yexport = yalibrary.tools.tool('yexport')
    ymake = yalibrary.tools.tool('ymake')

    sem_graph_path = os.path.join(GRADLE_DIR, 'sem.json')
    sem_graph = open(sem_graph_path, 'w')

    ymake_args = (
        '-k',
        '--build-root', GRADLE_DIR,
        '--config', conf,
        '--plugins-root', os.path.join(params.arc_root, 'build/plugins'),
        '--xs',
        '--sem-graph',
        os.path.join(params.arc_root, params.rel_targets[0]))

    def listener(event):
        logger.info(event)

    exit_code, stdout, stderr = run_ymake.run(ymake, ymake_args, {}, listener, raw_cpp_stdout=False)

    sem_graph.write(stdout)
    sem_graph.close()

    # debug info
    logger.info('Doing gradle\n' + params.__str__() + '\n' + yexport
                + '\n' + ymake + '\n' + conf + '\ngradle_dir ' + GRADLE_DIR + '\n' + sem_graph_path)

    project_name = target.split(os.sep)[-1]

    if params.gradle_name:
        project_name = params.gradle_name

    yexport_args = (yexport,
                    '--arcadia-root', params.arc_root,
                    '--export-root', os.path.join(params.arc_root, params.rel_targets[0]),
                    '-s', sem_graph_path,
                    '-G', 'ide-gradle',
                    '-t', project_name
                    )

    subprocess.call(yexport_args)

    find_all_modules(sem_graph_path, project_prefix)

    # бежим по всем .gradle, компилим некотрибные зависимости и подставляем их в скомпиленном виде
    go_through_gradle(os.path.join(params.arc_root, params.rel_targets[0]), resolve_non_contrib_peerdir, params,
                      'build.gradle.kts', True)

    build_target('foo', params)

    go_through_gradle(os.path.join(params.arc_root, params.rel_targets[0]), resolve_non_contrib_peerdir, params,
                      'build.gradle.kts', False)

    fix_settings_gradle(params)

    # чистим все после генерации
    delete_useless_folders(os.path.join(params.arc_root, params.rel_targets[0]))
    finish_time = time.time()
    logger.info('Taken time:')
    logger.info(finish_time - start_time)
