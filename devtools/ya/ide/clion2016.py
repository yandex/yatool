from __future__ import absolute_import
import os
import xml.etree.ElementTree as eTree
import sys
import six

from exts import fs

import build.build_opts
import build.graph_path
import core.yarg

from . import ide_common


MAKE_LIST_HEADER = '# fake CMakeLists.txt generated for CLion'
MAKE_LIST = 'CMakeLists.txt'


class MakeListConflictException(Exception):
    mute = True


def find_excludes(main_root, fringe, recursive_includes):
    exclude_roots = set()

    def go(root, fringe):
        fringe_roots = set([x[0] for x in fringe])
        for x in os.listdir(root):
            cur = os.path.join(root, x)
            if os.path.isdir(cur):
                if os.path.relpath(cur, main_root) in recursive_includes:
                    continue
                if x in fringe_roots:
                    go(cur, [x[1:] for x in fringe if len(x) > 1])
                else:
                    exclude_roots.add(cur)

    go(main_root, fringe)

    return exclude_roots


def write_xml(root, outpath):
    def _indent(elem, level=0):
        ind = "\n" + level * " " * 4
        if len(elem):
            if not elem.text or not elem.text.strip():
                elem.text = ind + " " * 4
            if not elem.tail or not elem.tail.strip():
                elem.tail = ind
            for elem in elem:
                _indent(elem, level + 1)
            if not elem.tail or not elem.tail.strip():
                elem.tail = ind
        else:
            if level and (not elem.tail or not elem.tail.strip()):
                elem.tail = ind

    _indent(root)
    eTree.ElementTree(root).write(outpath)


def get_vcs(arc_root):
    from yalibrary.vcs import detect

    vcs_type, _, _ = detect([arc_root])

    if not vcs_type:
        return 'svn'

    if vcs_type[0] == 'arc':
        return 'Arc'  # IntelliJ plugin uses VCS name "Arc", not "arc"

    return vcs_type[0]


def create_plugin_config(project_info):
    config_path = os.path.join(project_info.output_path, 'ya-settings.xml')
    root = eTree.Element('root')
    cmd = eTree.SubElement(root, 'cmd')
    for c in sys.argv:
        eTree.SubElement(cmd, 'part').text = c
    eTree.SubElement(root, 'cwd').text = os.getcwd()
    write_xml(root, config_path)


def gen_idea_prj(
    project_info,
    fringe,
    recursive_includes={},
    targets={},
    remote_toolchain=None,
    remote_deploy_config=None,
    remote_repo_path=None,
    remote_build_path=None,
    remote_host=None,
    use_sync_server=False,
    content_root=None,
):
    targets = dict(
        {ide_common.JOINT_TARGET_NAME: {'runnable': False}, ide_common.CODEGEN_TARGET_NAME: {'runnable': False}},
        **targets
    )
    exclude_roots = find_excludes(
        project_info.params.arc_root, sorted([x.split('/') for x in fringe]), recursive_includes
    )
    idea_dir = os.path.join(project_info.output_path, '.idea')
    if content_root is None:
        content_root = project_info.params.arc_root
    else:
        content_root = os.path.abspath(content_root)
    fs.create_dirs(idea_dir)

    # Write misc.xml

    with open(os.path.join(idea_dir, 'misc.xml'), 'w') as misc_out:
        misc_out.write(
            """<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
<component name="CMakeWorkspace" PROJECT_DIR="$PROJECT_DIR$">
<contentRoot DIR="{}" />
</component>
  <component name="CidrRootsConfiguration">""".format(
                content_root
            )
        )

        misc_out.write("<sourceRoots>\n")
        misc_out.write('<file path="{}" />\n'.format(content_root))
        misc_out.write("</sourceRoots>\n")

        misc_out.write("<excludeRoots>\n")
        for x in exclude_roots:
            misc_out.write('<file path="{}" />\n'.format(x))
        misc_out.write('<file path="$PROJECT_DIR$/_current_build_target" />')
        misc_out.write('<file path="$PROJECT_DIR$/sync.py" />')
        misc_out.write('<file path="$PROJECT_DIR$/sync.lock" />')
        misc_out.write("</excludeRoots>\n")

        misc_out.write('<libraryRoots>')
        misc_out.write('<file path="{}" />'.format(os.path.join(project_info.params.arc_root, 'contrib')))
        misc_out.write('</libraryRoots>')

        misc_out.write(
            """</component>
<component name="ProjectRootManager" version="2" />
<component name="SvnBranchConfigurationManager">
<option name="mySupportsUserInfoFilter" value="true" />
</component>
</project>"""
        )

    # Write vcs.xml

    vcs_file = os.path.join(idea_dir, 'vcs.xml')
    if not os.path.exists(vcs_file):
        with open(vcs_file, 'w') as vcs_out:
            vcs_out.write(
                """<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="VcsDirectoryMappings">
    <mapping directory="{root}" vcs="{vcs}" />
  </component>
</project>""".format(
                    root=project_info.params.arc_root, vcs=get_vcs(project_info.params.arc_root)
                )
            )

    # Write workspace.xml

    workspace_file = os.path.join(idea_dir, 'workspace.xml')
    generation_options = '-DCMAKE_SKIP_PREPROCESSED_SOURCE_RULES=ON -DCMAKE_SKIP_ASSEMBLY_SOURCE_RULES=ON'
    configs = [
        ("Debug", "Debug", generation_options, 'cmake-build-debug', False),
        ("Release", "Release", generation_options, 'cmake-build-release', False),
        ("Profile", "Profile", generation_options, 'cmake-build-profile', False),
        ("Asan", "Debug", generation_options + " -DYA_SANITIZE=address", 'cmake-build-asan', False),
        ("Msan", "Debug", generation_options + " -DYA_SANITIZE=memory", 'cmake-build-msan', False),
        ("Tsan", "Debug", generation_options + " -DYA_SANITIZE=thread", 'cmake-build-tsan', False),
        ("UBsan", "Debug", generation_options + " -DYA_SANITIZE=undefined", 'cmake-build-ubsan', False),
        ("Lsan", "Debug", generation_options + " -DYA_SANITIZE=leak", 'cmake-build-lsan', False),
        ("Remote Debug", "Debug", generation_options + " -DYA_REMOTE=ON", 'cmake-build-remote-debug', True),
        ("Remote Release", "Release", generation_options + " -DYA_REMOTE=ON", 'cmake-build-remote-release', True),
        ("Remote Profile", "Profile", generation_options + " -DYA_REMOTE=ON", 'cmake-build-remote-profile', True),
        (
            "Remote Asan",
            "Debug",
            generation_options + " -DYA_SANITIZE=address -DYA_REMOTE=ON",
            'cmake-build-remote-asan',
            True,
        ),
        (
            "Remote Msan",
            "Debug",
            generation_options + " -DYA_SANITIZE=memory -DYA_REMOTE=ON",
            'cmake-build-remote-msan',
            True,
        ),
        (
            "Remote Tsan",
            "Debug",
            generation_options + " -DYA_SANITIZE=thread -DYA_REMOTE=ON",
            'cmake-build-remote-tsan',
            True,
        ),
        (
            "Remote UBsan",
            "Debug",
            generation_options + " -DYA_SANITIZE=undefined -DYA_REMOTE=ON",
            'cmake-build-remote-ubsan',
            True,
        ),
        (
            "Remote Lsan",
            "Debug",
            generation_options + " -DYA_SANITIZE=leak -DYA_REMOTE=ON",
            'cmake-build-remote-lsan',
            True,
        ),
    ]

    if os.path.exists(workspace_file):
        with open(workspace_file) as f:
            root = eTree.fromstring(f.read())
    else:
        root = eTree.fromstring(
            '''<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
    <component name="CMakeRunConfigurationManager" shouldGenerate="false">
        <generated />
    </component>
</project>'''
        )

    def _warn_override(what, where_type, where, value, canon):
        if value != canon:
            ide_common.emit_message(
                '[[imp]]{}[[rst]] of your {} {!r} [[imp]]was overwritten[[rst]]: old value is {!r}, new value is {!r}'.format(
                    what, where_type, where, value, canon
                )
            )

    component = root.find('component[@name="CMakeSettings"]')
    if component is None:
        component = eTree.SubElement(root, 'component')
        component.set('name', 'CMakeSettings')
        component.set('AUTO_RELOAD', 'true')
    configurations = component.find('configurations')
    if configurations is None:
        configurations = eTree.SubElement(component, 'configurations')
    for profile_name, config_name, generation_options, generation_dir, is_remote in configs:
        configuration = configurations.find('configuration[@PROFILE_NAME="{}"]'.format(profile_name))
        if configuration is None:
            if is_remote and remote_toolchain is None:
                continue
            configuration = eTree.SubElement(configurations, 'configuration')
        else:
            if is_remote and remote_toolchain is None:
                configurations.remove(configuration)
                continue
            _warn_override('Name', 'CMake profile', profile_name, configuration.get('PROFILE_NAME'), profile_name)
            _warn_override('Build level', 'CMake profile', profile_name, configuration.get('CONFIG_NAME'), config_name)
            _warn_override(
                'CMake flags',
                'CMake profile',
                profile_name,
                configuration.get('GENERATION_OPTIONS'),
                generation_options,
            )
            _warn_override(
                'CMake output dir', 'CMake profile', profile_name, configuration.get('GENERATION_DIR'), generation_dir
            )
            if is_remote and remote_toolchain is not None:
                _warn_override(
                    'Toolchain', 'CMake profile', profile_name, configuration.get('TOOLCHAIN_NAME'), remote_toolchain
                )
        configuration.set('PROFILE_NAME', profile_name)
        configuration.set('CONFIG_NAME', config_name)
        configuration.set('GENERATION_OPTIONS', generation_options)
        configuration.set('GENERATION_DIR', generation_dir)
        if is_remote and remote_toolchain is not None:
            configuration.set('TOOLCHAIN_NAME', remote_toolchain)

    component = root.find('component[@name="RunManager"]')
    if component is None:
        component = eTree.SubElement(root, 'component')
        component.set('name', 'RunManager')
    for name, params in targets.items():
        configuration = component.find('configuration[@name="{}"]'.format(name))
        if configuration is None:
            configuration = eTree.SubElement(component, 'configuration')
            method = eTree.SubElement(configuration, 'method')
            method.set('v', '2')
            option = eTree.SubElement(method, 'option')
            option.set('name', 'com.jetbrains.cidr.execution.CidrBuildBeforeRunTaskProvider$BuildBeforeRunTask')
            option.set('enabled', 'true')
        else:
            _warn_override('Name', 'run/debug configuration', name, configuration.get('name'), name)
            _warn_override('Type', 'run/debug configuration', name, configuration.get('type'), 'CMakeRunConfiguration')
            _warn_override(
                'Factory name', 'run/debug configuration', name, configuration.get('factoryName'), 'Application'
            )
            _warn_override(
                'Project name', 'run/debug configuration', name, configuration.get('PROJECT_NAME'), project_info.title
            )
            _warn_override('Target', 'run/debug configuration', name, configuration.get('TARGET_NAME'), name)
            _warn_override('Config', 'run/debug configuration', name, configuration.get('CONFIG_NAME'), 'Debug')
            if params['runnable']:
                _warn_override(
                    'Executable',
                    'run/debug configuration',
                    name,
                    configuration.get('RUN_PATH'),
                    '$PROJECT_DIR$/_current_build_target/{}'.format(params['path']),
                )
        configuration.set('name', name)
        configuration.set('type', 'CMakeRunConfiguration')
        configuration.set('factoryName', 'Application')
        configuration.set('PROJECT_NAME', project_info.title)
        configuration.set('TARGET_NAME', name)
        configuration.set('CONFIG_NAME', 'Debug')
        if params['runnable']:
            configuration.set('RUN_PATH', '$PROJECT_DIR$/_current_build_target/{}'.format(params['path']))
    if remote_toolchain is not None:
        if component.find('configuration[@name="start sync server"]') is None:
            configuration = eTree.SubElement(component, 'configuration')
            method = eTree.SubElement(configuration, 'method')
            method.set('v', '2')
            configuration.set('name', 'start sync server')
            configuration.set('type', 'ShConfigurationType')
            options = [
                ('INDEPENDENT_SCRIPT_PATH', 'true'),
                ('SCRIPT_PATH', '$PROJECT_DIR$/sync.py'),
                ('SCRIPT_OPTIONS', 'server'),
                ('INDEPENDENT_SCRIPT_WORKING_DIRECTORY', 'true'),
                ('SCRIPT_WORKING_DIRECTORY', '$PROJECT_DIR$'),
                ('INDEPENDENT_INTERPRETER_PATH', 'true'),
                ('INTERPRETER_PATH', '/usr/bin/env'),
                ('INTERPRETER_OPTIONS', 'python3'),
            ]
            for name, value in options:
                option = eTree.SubElement(configuration, 'option')
                option.set('name', name)
                option.set('value', value)

    configurations_list = component.find('list')
    if configurations_list is None:
        configurations_list = eTree.SubElement(component, 'list')
    for name, params in targets.items():
        list_item = configurations_list.find('item[@itemvalue="CMake Application.{}"]'.format(name))
        if list_item is None:
            list_item = eTree.SubElement(configurations_list, 'item')
            list_item.set('itemvalue', 'CMake Application.{}'.format(name))
    if remote_toolchain is not None:
        list_item = configurations_list.find('item[@itemvalue="Shell Script.start sync server"]')
        if list_item is None:
            list_item = eTree.SubElement(configurations_list, 'item')
            list_item.set('itemvalue', 'Shell Script.start sync server')

    write_xml(root, workspace_file)

    if remote_repo_path is not None:
        # write deployment.xml

        deployment_file = os.path.join(idea_dir, 'deployment.xml')
        if os.path.exists(deployment_file):
            with open(deployment_file) as f:
                root = eTree.fromstring(f.read())
        else:
            root = eTree.fromstring(
                '''<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="PublishConfigData">
    <serverData>
    </serverData>
  </component>
</project>'''
            )

        component = root.find('component[@name="PublishConfigData"]')
        if component is None:
            component = eTree.SubElement(root, 'component')
            component.set('name', 'PublishConfigData')
        server_data = component.find('serverData')
        if server_data is None:
            server_data = eTree.SubElement(component, 'serverData')
        paths = server_data.find('paths[@name="{}"]'.format(remote_deploy_config))
        if paths is None:
            paths = eTree.SubElement(server_data, 'paths')
            paths.set('name', remote_deploy_config)
        paths_server_data = paths.find('serverdata')
        if paths_server_data is None:
            paths_server_data = eTree.SubElement(paths, 'serverdata')

        mappings = paths_server_data.find('mappings')
        if mappings is None:
            mappings = eTree.SubElement(paths_server_data, 'mappings')
        local_repo_path = os.path.join(
            '$PROJECT_DIR$', os.path.relpath(project_info.params.arc_root, project_info.output_path)
        )
        for deploy, local in [(remote_build_path, '$PROJECT_DIR$'), (remote_repo_path, local_repo_path)]:
            mapping = mappings.find('mapping[@local="{}"]'.format(local))
            if mapping is None:
                mapping = eTree.SubElement(mappings, 'mapping')
            mapping.set('deploy', deploy)
            mapping.set('local', local)

        excluded_paths = paths_server_data.find('excludedPaths')
        if excluded_paths is None:
            excluded_paths = eTree.SubElement(paths_server_data, 'excludedPaths')
        generation_dirs = ['$PROJECT_DIR$/{}'.format(generation_dir) for (_, _, _, generation_dir, _) in configs]
        generation_dirs += ['$PROJECT_DIR$/_current_build_target', project_info.params.arc_root]
        for generation_dir in generation_dirs:
            excluded_path = excluded_paths.find('excludedPath[@path="{}"]'.format(generation_dir))
            if excluded_path is None:
                excluded_path = eTree.SubElement(excluded_paths, 'excludedPath')
                excluded_path.set('path', generation_dir)
                excluded_path.set('local', 'true')

        write_xml(root, deployment_file)

        # write externalDependencies.xml

        external_dependencies_file = os.path.join(idea_dir, 'externalDependencies.xml')
        if not os.path.exists(external_dependencies_file):
            with open(external_dependencies_file, 'w') as vcs_out:
                vcs_out.write(
                    """<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="ExternalDependencies">
    <plugin id="com.intellij.plugins.watcher" />
  </component>
</project>"""
                )

        # write watcherTasks.xml

        watcher_tasks_file = os.path.join(idea_dir, 'watcherTasks.xml')
        if os.path.exists(watcher_tasks_file):
            with open(watcher_tasks_file) as f:
                root = eTree.fromstring(f.read())
        else:
            root = eTree.fromstring(
                '''<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="ProjectTasksOptions">
  </component>
</project>
'''
            )

        component = root.find('component[@name="ProjectTasksOptions"]')
        if component is None:
            component = eTree.SubElement(root, 'component')
            component.set('name', 'ProjectTasksOptions')
        task = component.find('TaskOptions/option[@name="name"][@value="sync local changes"]/..')
        if task is None:
            task = eTree.SubElement(component, 'TaskOptions')
            task.set('isEnabled', 'true' if not use_sync_server else 'false')
        if use_sync_server and task.get('isEnabled', 'false') == 'true':
            ide_common.emit_message(
                'File watcher [[imp]]sync local changes[[rst]] '
                'is still enabled. Disable it manually if you only want to '
                'synchronize changes when building.'
            )
        elif not use_sync_server and task.get('isEnabled', 'false') == 'false':
            ide_common.emit_message(
                '[[warn]]Warning[[rst]]: file watcher '
                '[[imp]]sync local changes[[rst]] is disabled. '
                'Local changes are not being synchronized to the remote host.'
            )

        def set_opt(name, value):
            opt = task.find('option[@name="{}"]'.format(name))
            if opt is None:
                opt = eTree.SubElement(task, 'option')
            opt.set('name', name)
            opt.set('value', value)

        set_opt("arguments", "")
        set_opt("checkSyntaxErrors", "false")
        set_opt("exitCodeBehavior", "ERROR")
        set_opt("fileExtension", "*")
        set_opt("immediateSync", "true")
        set_opt("name", "sync local changes")
        set_opt("output", "")
        set_opt("outputFromStdout", "false")
        set_opt("program", "$ProjectFileDir$/sync.py")
        set_opt("runOnExternalChanges", "true")
        set_opt("scopeName", "Project Source Files")
        set_opt("trackOnlyRoot", "false")
        set_opt("workingDir", "")

        write_xml(root, watcher_tasks_file)

        # write sync.py

        sync_file = os.path.join(project_info.output_path, 'sync.py')
        with open(sync_file, 'w') as sync_file_out:
            import __res

            script = (
                six.ensure_str(__res.find('/clion/sync.py'))
                .replace('<<host>>', remote_host)
                .replace('<<remote_repo>>', ide_common.fix_win_path(remote_repo_path))
                .replace('<<remote_build>>', ide_common.fix_win_path(remote_build_path))
                .replace('<<local_repo>>', ide_common.fix_win_path(project_info.params.arc_root))
                .replace('<<local_build>>', ide_common.fix_win_path(project_info.output_path))
            )
            sync_file_out.write(script)
        os.chmod(sync_file, 0o774)


def gen_lite_solution(info, filters, required_cmake_version):
    if not filters:
        filters = ['.']
    for proj_dir in [os.path.normpath(os.path.join(os.getcwd(), i)) for i in filters]:
        ide_common.emit_message("Generate lite project for [[imp]]{}[[rst]]".format(proj_dir))
        with open(os.path.join(proj_dir, MAKE_LIST), 'w') as solution:
            solution.write(
                '''cmake_minimum_required(VERSION {cmake_version})
set(CMAKE_CXX_STANDARD 17)

include_directories({arc_root})

project({proj_name})
file(GLOB_RECURSE SOURCE_FILES
        ${{CMAKE_CURRENT_SOURCE_DIR}}/*.cpp
        ${{CMAKE_CURRENT_SOURCE_DIR}}/*.proto
        ${{CMAKE_CURRENT_SOURCE_DIR}}/*.sh
        ${{CMAKE_CURRENT_SOURCE_DIR}}/*.py
        ${{CMAKE_CURRENT_SOURCE_DIR}}/*.txt)
add_executable({proj_name} ${{SOURCE_FILES}})
set_target_properties({proj_name} PROPERTIES LINKER_LANGUAGE CXX)
'''.format(
                    cmake_version=required_cmake_version,
                    arc_root=info.params.arc_root,
                    proj_name=os.path.basename(proj_dir),
                )
            )
        ide_common.emit_message("File to open [[imp]]{}[[rst]]".format(os.path.join(proj_dir, MAKE_LIST)))


def do_clion(params):
    import app_ctx

    ide_common.emit_message('[[bad]]ya ide clion is deprecated, please use clangd-based tooling instead')

    if not params.full_targets:
        if params.add_py_targets:
            params.ya_make_extra.append('-DBUILD_LANGUAGES=CPP PY3')
        else:
            params.ya_make_extra.append('-DBUILD_LANGUAGES=CPP')

    ya_make_opts = core.yarg.merge_opts(build.build_opts.ya_make_options(free_build_targets=True))
    params = core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra), params)

    cmake_stub_info = ide_common.IdeProjectInfo(params, app_ctx, default_output_here=True)
    if params.lite_mode:
        gen_lite_solution(cmake_stub_info, params.filters, '3.20')
    else:
        remote_toolchain = params.remote_toolchain
        remote_deploy_config = params.remote_deploy_config
        remote_repo_path = params.remote_repo_path
        remote_build_path = params.remote_build_path
        remote_host = params.remote_deploy_host

        if (
            remote_toolchain is not None
            or remote_deploy_config is not None
            or remote_repo_path is not None
            or remote_build_path is not None
            or remote_host is not None
        ):
            # YA_IDE_CLION_REMOTE_FORCE used in tests to force generation even if this is not an arc repo
            if get_vcs(cmake_stub_info.params.arc_root) != 'Arc' and 'YA_IDE_CLION_REMOTE_FORCE' not in os.environ:
                ide_common.emit_message('remote build only works with arc repository')
                return
            if remote_toolchain is None:
                ide_common.emit_message('[[imp]]--remote-toolchain[[rst]] is required')
                return
            if remote_deploy_config is None:
                ide_common.emit_message('[[imp]]--remote-deploy-config[[rst]] is required')
                return
            if remote_repo_path is None:
                ide_common.emit_message('[[imp]]--remote-repo-path[[rst]] is required')
                return
            if remote_build_path is None:
                ide_common.emit_message('[[imp]]--remote-build-path[[rst]] is required')
                return
            if remote_host is None:
                ide_common.emit_message('[[imp]]--remote-host[[rst]] is required')
                return

        cmake_stub = ide_common.CMakeStubProject(params, app_ctx, cmake_stub_info, required_cmake_version='3.20')
        cmake_stub.generate(
            filters=params.filters,
            forbid_cmake_override=True,
            joint_target=True,
            codegen_target=True,
            out_dir='${CMAKE_CURRENT_SOURCE_DIR}/_current_build_target',
            remote_dir=params.remote_repo_path,
            use_sync_server=params.use_sync_server,
            strip_non_final=params.strip_non_final_targets,
        )
        source_roots = {build.graph_path.GraphPath(os.path.dirname(x)).strip() for x in cmake_stub.project_files}
        inc_dirs = {build.graph_path.GraphPath(x).strip() for x in cmake_stub.inc_dirs}
        source_roots.update(inc_dirs)
        gen_idea_prj(
            cmake_stub_info,
            source_roots,
            inc_dirs,
            cmake_stub.targets,
            remote_toolchain,
            remote_deploy_config,
            remote_repo_path,
            remote_build_path,
            remote_host,
            params.use_sync_server,
            params.content_root,
        )
    create_plugin_config(cmake_stub_info)

    if params.setup_tidy:
        source_root = os.path.abspath(params.content_root) if params.content_root else cmake_stub_info.params.arc_root
        ide_common.setup_tidy_config(source_root)
