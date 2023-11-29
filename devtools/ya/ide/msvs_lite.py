from __future__ import absolute_import
from __future__ import print_function
import os
import re
import bisect
import logging
import itertools

import app
import exts
from . import msvs
from . import msbuild
import ide.ide_common
import build.gen_plan2
import build.build_facade

from . import msvs_lite_utils as utils

logger = logging.getLogger(__name__)

_VERSIONS = ('2017', '2019')


class Error(Exception):
    mute = True


class Solution(object):
    _SUPPORTED_VERSIONS = tuple(sorted(_VERSIONS))

    solution_path = msvs.MsvsSolution.solution_path

    def __init__(self, params, app_ctx, info):
        self.params = params
        self.app_ctx = app_ctx
        self.info = info
        app_ctx.display.emit_message('Using Visual Studio version: [[imp]]{}[[rst]]'.format(self.version))

    def generate(self):
        ide_graph = ide.ide_common.IdeGraph(self.params)
        generator = SlnGenerator(self.params, self.info, ide_graph, self.tools_version, self.proj_root)
        generator.generate_sln()
        generator.generate_projects()

    @property
    def proj_root(self):
        return os.path.join(self.info.instance_path, 'Projects')

    @property
    def version(self):
        v = bisect.bisect(self._SUPPORTED_VERSIONS, self.params.project_version)
        if not v:
            raise Error('Visual Studio {} is unsupported in lite mode'.format(self.params.project_version))
        return self._SUPPORTED_VERSIONS[v - 1]

    @property
    def tools_version(self):
        if self.version == '2017':
            return 141
        if self.version == '2019':
            return 142
        assert False


class ProjectDescription(object):
    INCLUDE_SOURCE_EXTS = ('.h', '.hh', '.hpp', '.inc', '')
    BUILD_SOURCE_EXTS = (
        '.cpp', '.cc', '.c', '.cxx', '.C', '.rl5', '.rl6', '.rl', '.gperf', '.y', '.ypp', '.l', '.lex', '.lpp',
        '.proto', '.gztproto', '.ev', '.asp', '.fml', '.fml2', '.fml3', '.sfdl', '.S', '.asm', '.masm', '.cu', '.swg',
        '.pyx', '.xsyn', '.py'
    )
    EXCLUDE_SOURCE_EXTS = ('.obj', '.exe', '.lib', '.dll', '.o', '.a', '.so')
    EXCLUDE_FILES = ('build/scripts/fix_msvc_output.py', 'build/scripts/_fake_src.cpp', 'build/scripts/link_lib.py')

    def __init__(self):
        self.path = None
        self.name = None
        self.sources = None
        self.type = None
        self.outputs = []
        self.deps = []
        self._guid = None

    def includes(self):
        return [include for include in self.sources if utils.get_ext(include) in self.INCLUDE_SOURCE_EXTS]

    def build_files(self):
        return [src for src in self.sources if utils.get_ext(src) not in self.INCLUDE_SOURCE_EXTS]

    def external_files(self):
        build_srcs = [src for src in self.sources if utils.get_ext(src) in self.BUILD_SOURCE_EXTS]
        dirs = set([msbuild.norm_path(os.path.dirname(src)) for src in build_srcs])
        return [src for src in self.sources if msbuild.norm_path(os.path.dirname(src)) not in dirs]

    def external_includes(self):
        return [ext for ext in self.external_files() if utils.get_ext(ext) in self.INCLUDE_SOURCE_EXTS]

    def external_build_files(self):
        return [ext for ext in self.external_files() if utils.get_ext(ext) not in self.INCLUDE_SOURCE_EXTS]

    @property
    def guid(self):
        if not self._guid:
            self._guid = utils.to_guid(self.path)
        return self._guid

    def __str__(self):
        template = 'path: {}\nname: {}\ntype: {}\nguid: {}\ndeps:\n{}\nsources:\n{}\n'
        deps = ''
        for dep in self.deps:
            deps += '    {}\n'.format(dep)
        sources = ''
        for src in sorted(self.sources):
            sources += '    {}\n'.format(src)
        return template.format(self.path, self.name, self.type, self.guid, deps, sources)


class ProjectDependency(object):
    def __init__(self, path, guid):
        self.path = path
        self.guid = guid

    def __str__(self):
        return '{} - {}'.format(self.path, self.guid)


class LookUpProjects(object):
    def __init__(self, params):
        self.params = params
        self.graph = self._prepare_graph(params)

    def go(self):
        store_uids = {}  # {uid: outputs[0}
        store_descriptions = []
        top_proj_descr = self._create_top_description()

        for node in self.graph['graph']:
            if node['target_properties'] and 'module_type' in node['target_properties']:
                description = self._create_description(node)
                store_uids[node['uid']] = description.path
                store_descriptions.append(description)
                top_proj_descr.outputs.extend(description.outputs)

        store_descriptions = self._clean_deps(store_descriptions, store_uids)
        store_descriptions = self._remove_extra_inputs(store_descriptions)
        return top_proj_descr, store_descriptions

    def _clean_deps(self, store_descriptions, store_uids):
        for descr in store_descriptions:
            for uid in list(descr.deps):
                descr.deps.remove(uid)
                if uid in store_uids:
                    descr.deps.append(ProjectDependency(store_uids[uid], utils.to_guid(store_uids[uid])))
        return store_descriptions

    def _create_description(self, node):
        description = ProjectDescription()
        output = self._remove_dollar(msbuild.norm_path(node['outputs'][0]))
        description.path = '{}.vcxproj'.format(os.path.splitext(output)[0])
        description.sources = self._clean_sources(node['inputs'], description.path)
        description.name = os.path.splitext(os.path.basename(output))[0]
        description.outputs = [output]
        description.type = node['target_properties']['module_type']
        description.deps = node['deps']
        return description

    def _create_top_description(self):
        top_descr = ProjectDescription()
        top_descr.path = 'all_arcadia.vcxproj'
        top_descr.name = msbuild.norm_path(self.params.rel_targets[0]) + '/' + 'all_arcadia'
        top_descr.type = 'top'
        top_descr.deps = []
        return top_descr

    def _prepare_graph(self, params):
        params.strict_inputs = True
        params.dump_graph = True
        params.build_threads = 0
        params.debug_options = ['x']
        graph = build.gen_plan2.ya_make_graph(params, app, real_ya_make_opts=True)
        return graph

    def _remove_dollar(self, path):
        return msbuild.norm_path(re.sub(r'\$[(]{0,1}\w+[)]{0,1}[\\/]', '', path))

    def _clean_sources(self, sources, root_path):
        clean_sources = []
        for src in sources:
            src = self._remove_dollar(src)
            if utils.get_ext(src) not in ProjectDescription.EXCLUDE_SOURCE_EXTS and src not in ProjectDescription.EXCLUDE_FILES:
                clean_sources.append(src)
        return sorted(set(clean_sources))

    def _remove_extra_inputs(self, descriptions):
        new_descriptions = list(descriptions)
        for d1 in descriptions:
            for d2 in new_descriptions:
                d2_guids = [dep.guid for dep in d2.deps]
                if d1.guid in d2_guids:
                    d2.sources = set(d2.sources) - set(d1.sources)
        return new_descriptions


class SlnGenerator(object):
    def __init__(self, params, info, ide_graph, tools_version, proj_root):
        self.params = params
        self.extra_args = params.ya_make_extra
        self.info = info
        self.ide_graph = ide_graph
        self.tools_version = tools_version
        self.proj_root = proj_root
        self.top_description, self.proj_descriptions = LookUpProjects(self.params).go()

    def generate_sln(self):
        proj = SlnProject(self.proj_descriptions + [self.top_description], self.info, self.proj_root)
        proj.dump_sln()

    def generate_projects(self):
        projects = []
        for proj_descr in self.proj_descriptions + [self.top_description]:
            if proj_descr.type == 'top':
                projects.append(msbuild.TopProject(proj_descr, self.top_description, self.ide_graph, self.info, self.tools_version, self.proj_root, self.extra_args))
            else:
                projects += [
                    msbuild.GeneralProject(proj_descr, self.top_description, self.ide_graph, self.info, self.tools_version, self.proj_root, self.extra_args),
                    msbuild.FilterProject(proj_descr, self.top_description, self.ide_graph, self.info, self.tools_version, self.proj_root),
                    msbuild.DebugInfoProject(proj_descr, self.top_description, self.ide_graph, self.info, self.tools_version, self.proj_root)
                ]

        for project in projects:
            project.generate()
            project.dump()


class Node(object):
    def __init__(self, path, guid):
        self.path = path
        self.guid = guid
        self.children = []

    def __contains__(self, item):
        for child in self.children:
            if child.path == item:
                return True
        return False

    def get_child(self, path):
        for child in self.children:
            if path == child.path:
                return child
        return None

    def __str__(self):
        return '{} {}'.format(self.path, self.guid)


class SlnProject(object):
    def __init__(self, proj_descriptions, info, proj_root):
        self.proj_descriptions = proj_descriptions
        self.info = info
        self.proj_root = proj_root
        self.tree = self._generate_path_tree()

    @staticmethod
    def dump(node, tab=''):
        print('{}{}'.format(tab, node))
        for child in node.children:
            SlnProject.dump(child, '\t{}'.format(tab))

    def _generate_path_tree(self):
        rel_path = msbuild.norm_path(os.path.relpath(self.proj_root, self.info.instance_path))
        root_node = Node(rel_path, 0)

        gen_tree(root_node, self.proj_descriptions)
        for child in root_node.children:
            reorgonize_tree(child, root_node)

        # SlnProject.dump(root_node)
        return root_node

    def _iterate_tree(self):
        st = list([self.tree])
        while st:
            current = st.pop()
            if current.guid != 0:
                yield current
            for child in current.children:
                st.append(child)

    @property
    def sln_path(self):
        return os.path.join(self.info.instance_path, '{}.sln'.format(self.info.title))

    def get_sln(self):
        template = ['Microsoft Visual Studio Solution File, Format Version 12.00']

        for node in self._iterate_tree():
            template.extend(add_sln_project_node(node.path, node.guid))

        template.append('Global')

        template.append('\tGlobalSection(SolutionConfigurationPlatforms) = preSolution')
        for mode, platform in itertools.product(msbuild.MODES, msbuild.PLATFORMS):
            template.append('\t\t{mode}|{plat} = {mode}|{plat}'.format(mode=mode, plat=platform))
        template.append('\tEndGlobalSection')

        template.append('\tGlobalSection(ProjectConfigurationPlatforms) = postSolution')
        for node in self._iterate_tree():
            if node.path.endswith('.vcxproj'):
                for mode, platform in itertools.product(msbuild.MODES, msbuild.PLATFORMS):
                    template.append('\t\t{{{guid}}}.{mode}|{plat}.ActiveCfg = {mode}|{plat}'.format(
                        guid=node.guid, mode=mode, plat=platform
                    ))
                    template.append('\t\t{{{guid}}}.{mode}|{plat}.Build.0 = {mode}|{plat}'.format(
                        guid=node.guid, mode=mode, plat=platform
                    ))
        template.append('\tEndGlobalSection')

        template.append('\tGlobalSection(NestedProjects) = preSolution')
        for node in self._iterate_tree():
            for child in node.children:
                template.append('\t\t{{{}}} = {{{}}}'.format(child.guid, node.guid))

        template.append('\tEndGlobalSection')

        template.append('\tGlobalSection(ExtensibilityGlobals) = postSolution')
        template.append('\tEndGlobalSection')
        template.append('\tGlobalSection(ExtensibilityAddIns) = postSolution')
        template.append('\tEndGlobalSection')

        template.append('EndGlobal')

        return template

    def dump_sln(self):
        output = self.get_sln()
        exts.fs.create_dirs(os.path.dirname(self.sln_path))
        with open(self.sln_path, 'w') as f:
            for line in output:
                f.write(line)
                f.write('\n')


def reorgonize_tree(node, parent):
    if len(node.children) == 1:
        child = node.children[0]
        i = parent.children.index(node)
        parent.children.insert(i, child)
        parent.children.remove(node)
        reorgonize_tree(child, parent)
    else:
        for child in node.children:
            reorgonize_tree(child, node)


def add_sln_project_node(path, guid):
    if path.endswith('.vcxproj'):
        type = '8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942'  # C++ type
    else:
        type = '2150E333-8FDC-42A3-9474-1A3956D46DE8'  # Solution Folder type
    name = os.path.splitext(os.path.basename(path))[0]
    template = [
        'Project("{{{type}}}") = "{name}", "{path}", "{{{guid}}}"'.format(type=type, name=name, path=path, guid=guid),
        'EndProject'
    ]

    return template


def gen_tree(root_node, proj_descriptions):
    for p_descr in proj_descriptions:
        add_node(root_node, p_descr.path, p_descr.guid)


def add_node(root_node, path, guid):
    chunks = path.split('/')
    current_node = root_node
    for chunk in chunks[: -1]:
        current_path = msbuild.norm_path(os.path.join(current_node.path, chunk))
        if current_path not in current_node:
            current_node.children.append(Node(current_path, utils.to_guid(current_path)))
            current_node = current_node.children[-1]
        else:
            current_node = current_node.get_child(current_path)

    current_node.children.append(Node(msbuild.norm_path(os.path.join(root_node.path, path)), guid))


def get_res(res):
    import __res
    return __res.find(res)


class MsvsResource(object):
    def __init__(self, info):
        self.info = info

    def dump(self, res_key, transform=None, extra_path=None):
        res = transform(get_res(res_key)) if transform else get_res(res_key)
        exts.fs.create_dirs(self.info.instance_path)
        with open(self._res_path(res_key, extra_path), 'wb') as f:
            f.write(res)

    def _res_path(self, res_key, extra_path=None):
        if extra_path:
            return os.path.join(self.info.instance_path, extra_path, os.path.basename(res_key))
        return os.path.join(self.info.instance_path, os.path.basename(res_key))


def settings(template):
    return template.replace('COMMAND', 'ya.bat')


def gen_msvs_solution(params):
    import app_ctx  # XXX
    app_ctx.display.emit_message('[[bad]]ya ide msvs is deprecated, please use clangd-based tooling instead')
    app_ctx.display.emit_message('[[imp]]Generating Visual Studio solution[[rst]]')
    solution_info = ide.ide_common.IdeProjectInfo(params, app_ctx, default_output_name=msvs.DEFAULT_MSVS_OUTPUT_DIR)
    res = MsvsResource(solution_info)
    if params.install:
        res.dump('/msvs/settings/msvs_lite.vssettings', settings)
        app_ctx.display.emit_message(
            '[[good]]Settings for Visual Studio at: [[path]]{}[[rst]]'.format(settings.settings_path)
        )
    else:
        solution = Solution(params, app_ctx, solution_info)
        solution.generate()
        res.dump('arcadia.natvis', extra_path='Projects')
        app_ctx.display.emit_message(
            '[[good]]Ready. File to open: [[path]]{}[[rst]]'.format(solution.solution_path))
