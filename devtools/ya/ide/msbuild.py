from __future__ import absolute_import
import os
import itertools
import multiprocessing
import xml.etree.ElementTree as et

import exts

from . import msvs_lite_utils as utils

XMLNS = 'http://schemas.microsoft.com/developer/msbuild/2003'
PLATFORMS = ['x64']
MODES = ['Debug', 'Release', 'relwithdebinfo']
pretty = True
EXTERNAL_DIR = 'External'


class Project(object):
    """
        MSBuild project
        https://msdn.microsoft.com/en-us/library/bcxfsh87.aspx
    """

    def __init__(self, proj_descr, main_descr, ide_graph, info, tools_version=None, root_path=None):
        self.proj_descr = proj_descr
        self.main_descr = main_descr
        self.ide_graph = ide_graph
        self.info = info
        self.tools_version = tools_version
        self.root_path = root_path
        attrs = {
            'xmlns': XMLNS,
            # 'DefaultTargets': 'Build',
        }
        if tools_version is not None and self.msbuildtoolsversion:
            attrs['ToolsVersion'] = self.msbuildtoolsversion
        self._xml = et.ElementTree(et.Element('Project', attrs))

    @property
    def msbuildtoolsversion(self):
        if self.tools_version == 140:
            return '14.0'
        elif self.tools_version == 141:
            return '15.0'
        elif self.tools_version == 142:
            return None  # MSVS doesn't fill this property now
        assert False

    @property
    def xml_root(self):
        return self._xml.getroot()

    @property
    def source_root(self):
        return norm_path(self.info.params.arc_root)

    @property
    def build_root(self):
        return '$(SolutionDir)$(Configuration)'

    @property
    def intdir(self):
        return norm_path(os.path.join(self.build_root, 'MSVS', self.proj_descr.guid)) + '/'

    def store_path(self):
        if self.root_path:
            output_path = os.path.join(self.root_path, self.proj_descr.path)
        else:
            output_path = os.path.join(self.info.instance_path, self.proj_descr.path)
        exts.fs.create_dirs(os.path.dirname(output_path))
        return output_path

    def dump(self):
        with open(self.store_path(), 'w') as f:
            if pretty:
                f.write(make_pretty(self._xml.getroot()))
            else:
                self._xml.write(f, encoding='utf-8', xml_declaration=True)

    def generate(self):
        raise NotImplementedError


class TopProject(Project):
    def __init__(self, proj_descr, main_descr, ide_graph, info, tools_version=None, root_path=None, extra_args=None):
        super(TopProject, self).__init__(proj_descr, main_descr, ide_graph, info, tools_version, root_path)
        self.extra_args = extra_args

    def generate(self):
        add_project_configuration(self.xml_root)
        out_dir = norm_path(os.path.join(self.build_root, 'MSVS')) + '/'
        add_root_defines(self.xml_root, self.source_root, self.build_root, out_dir, self.intdir)
        add_project_properties(self.xml_root, os.path.basename(self.proj_descr.name), self.proj_descr.guid)

        add_node(self.xml_root, 'Import', attrib={'Project': r'$(VCTargetsPath)\Microsoft.Cpp.Default.props'})
        add_toolset_nodes(self.xml_root, self.tools_version)
        add_node(self.xml_root, 'Import', attrib={'Project': r'$(VCTargetsPath)\Microsoft.Cpp.props'})

        path = '$(SOURCE_ROOT)/' + norm_path(os.path.dirname(self.proj_descr.name))
        add_custom_command(self.xml_root, path, self.extra_args)

        add_node(self.xml_root, 'Import', attrib={'Project': r'$(VCTargetsPath)\Microsoft.Cpp.targets'})

        natvis = add_node(self.xml_root, 'ItemGroup')
        add_node(natvis, 'Natvis', attrib={'Include': 'arcadia.natvis'})


class GeneralProject(Project):
    def __init__(self, proj_descr, main_descr, ide_graph, info, tools_version=None, root_path=None, extra_args=None):
        super(GeneralProject, self).__init__(proj_descr, main_descr, ide_graph, info, tools_version, root_path)
        self.extra_args = extra_args

    def generate(self):
        add_project_configuration(self.xml_root)
        out_dir = norm_path(os.path.join(self.build_root, os.path.dirname(self.proj_descr.path))) + '/'
        add_root_defines(self.xml_root, self.source_root, self.build_root, out_dir, self.intdir)
        add_project_properties(self.xml_root, self.proj_descr.name, self.proj_descr.guid)

        add_node(self.xml_root, 'Import', attrib={'Project': r'$(VCTargetsPath)\Microsoft.Cpp.Default.props'})
        add_toolset_nodes(self.xml_root, self.tools_version)
        add_node(self.xml_root, 'Import', attrib={'Project': r'$(VCTargetsPath)\Microsoft.Cpp.props'})

        path = '$(SOURCE_ROOT)/' + norm_path(os.path.dirname(self.proj_descr.path))
        add_custom_command(self.xml_root, path, self.extra_args)

        inc_dirs = prepare_include_dirs(self.ide_graph.inc_dirs)
        add_item_definition(self.xml_root, inc_dirs, sorted(self.ide_graph.defines))

        includes = ['$(SOURCE_ROOT)/' + entry for entry in self.proj_descr.includes()]
        sources = ['$(SOURCE_ROOT)/' + entry for entry in self.proj_descr.build_files()]
        add_sources(self.xml_root, includes, sources)

        add_node(self.xml_root, 'Import', attrib={'Project': r'$(VCTargetsPath)\Microsoft.Cpp.targets'})


class FilterProject(Project):
    def __init__(self, proj_descr, main_descr, ide_graph, info, tools_version=None, root_path=None):
        super(FilterProject, self).__init__(proj_descr, main_descr, ide_graph, info, tools_version, root_path)

    def store_path(self):
        output_path = super(FilterProject, self).store_path()
        return '{}.filters'.format(output_path)

    def generate(self):
        exts_incl = ['$(SOURCE_ROOT)/' + entry for entry in self.proj_descr.external_includes()]
        exts_src = ['$(SOURCE_ROOT)/' + entry for entry in self.proj_descr.external_build_files()]
        add_externals(self.xml_root, exts_incl, exts_src)
        add_filter_description(self.xml_root, self.proj_descr.path)


class DebugInfoProject(Project):
    def __init__(self, proj_descr, main_descr, ide_graph, info, tools_version=None, root_path=None):
        super(DebugInfoProject, self).__init__(proj_descr, main_descr, ide_graph, info, tools_version, root_path)

    def store_path(self):
        output_path = super(DebugInfoProject, self).store_path()
        return '{project_path}.user'.format(project_path=output_path)

    def generate(self):
        env = {
            'ARCADIA_BUILD_ROOT': '$(BUILD_ROOT)',
            'ARCADIA_SOURCE_ROOT': '$(SOURCE_ROOT)'
        }
        add_debugger_env(self.xml_root, env)


def prepare_include_dirs(inc_dirs):
    inc_dirs = [subst_dots(dir_) for dir_ in inc_dirs if 'DEFAULT_WIN_X86_64' not in dir_]
    return sorted(norm_paths(inc_dirs))


def subst_dots(path):
    return path.replace('__', '..')


def norm_path(path):
    return os.path.normpath(path).replace('\\', '/')


def norm_paths(lst):
    for path in lst:
        yield norm_path(path)


def make_pretty(root):
    import xml.dom.minidom
    string = et.tostring(root, 'utf-8')
    parsed = xml.dom.minidom.parseString(string)
    return parsed.toprettyxml(indent='  ')


def add_node(root, name, attrib={}, text=None):
    a = et.SubElement(root, name, attrib=attrib)
    a.text = text
    return a


def add_project_configuration(root):
    def append_conf(root, mode, platform):
        conf_node = add_node(
            root, 'ProjectConfiguration', attrib={'Include': '{mode}|{platform}'.format(mode=mode, platform=platform)}
        )
        add_node(conf_node, 'Configuration', text=mode)
        add_node(conf_node, 'Platform', text=platform)

    conf = add_node(root, 'ItemGroup', attrib={'Label': 'ProjectConfigurations'})
    for mode, platform in itertools.product(MODES, PLATFORMS):
        append_conf(conf, mode, platform)


def add_project_properties(root, name, guid):
    property_node = add_node(root, 'PropertyGroup', attrib={'Label': 'Globals'})
    add_node(property_node, 'ProjectGUID', text='{{{}}}'.format(guid))
    add_node(property_node, 'ProjectName', text=name)


def add_root_defines(root, source_root, build_root, outdir, intdir):
    prop_node = add_node(root, 'PropertyGroup', attrib={'Label': 'GlobalDefines'})
    add_node(prop_node, 'SOURCE_ROOT', text=source_root)
    add_node(prop_node, 'BUILD_ROOT', text=build_root)
    add_node(prop_node, 'OutDir', text=outdir)
    add_node(prop_node, 'IntDir', text=intdir)


def add_sources(root, includes, sources):
    def add_includes(root):
        for incl in sorted(includes):
            attrib = {'Include': incl}
            add_node(root, 'ClInclude', attrib=attrib)

    def add_srcs(root):
        for src in sorted(sources):
            add_node(root, 'None', attrib={'Include': src})

    group = add_node(root, 'ItemGroup')
    add_includes(group)
    add_srcs(group)


def add_item_definition(root, inc_dirs, defines):
    item_def = add_node(root, 'ItemDefinitionGroup')
    clcompile = add_node(item_def, 'ClCompile')

    add_node(clcompile, 'AdditionalOptions', text=';'.join(defines))  # XXX dep from mode build
    add_node(clcompile, 'AdditionalIncludeDirectories', text=';'.join(inc_dirs))  # XXX


def add_toolset_nodes(root, tools_version):
    prop_node = add_node(root, 'PropertyGroup', attrib={'Label': 'Configuration'})
    add_node(prop_node, 'PlatformToolset', text='v{version}'.format(version=tools_version))


def gen_opts():
    exts = ['.h', '.cpp', '.cc', '.c', '.cxx', '.C', '.inc']
    return ['--add-result={}'.format(ext) for ext in exts]


def add_custom_command(root, path, extra_args=None):
    property_node = add_node(root, 'PropertyGroup')
    add_node(property_node, 'CustomBuildBeforeTargets', text='Build')

    item_def = add_node(root, 'ItemDefinitionGroup')
    cmd_node = add_node(item_def, 'CustomBuildStep')

    use_cpu = multiprocessing.cpu_count() - 1
    args = [
        '$(SOURCE_ROOT)/ya',
        'tool',
        'python',
        '--',
        '$(SOURCE_ROOT)/ya make',
        '-j{}'.format(use_cpu),
        '-T',
        '--no-emit-status',
        '--no-emit-nodes', 'CompactCache',
        '--no-emit-nodes', 'ResultNode',
        '--force-build-depends',
        '--build=$(Configuration)',
        '--output=$(BUILD_ROOT)',
    ]

    if extra_args:
        args += extra_args

    args += [
        '{opts}'.format(opts=' '.join(gen_opts())),
        '{path}'.format(path=path),
    ]

    add_node(cmd_node, 'Command', text=' '.join(args))
    add_node(cmd_node, 'Outputs', text='fake.output')  # Add non-existing file to outputs -> always run custom build step


def add_dependencies_node(root, rel_path, deps):
    group_node = add_node(root, 'ItemGroup')
    for dep in deps:
        ref_path = '$(SolutionDir)/{rel_path}/{path}'.format(rel_path=rel_path, path=dep.path)
        ref_node = add_node(group_node, 'ProjectReference', attrib={'Include': ref_path})
        add_node(ref_node, 'Project', text=dep.guid)


def add_externals(root, external_includes, external_sources):
    def add_filter(root, node_type, src):
        node = add_node(root, node_type, attrib={'Include': src})
        add_node(node, 'Filter', text=EXTERNAL_DIR)

    group_node = add_node(root, 'ItemGroup')
    for src in external_includes:
        add_filter(group_node, 'ClInclude', src)
    for src in external_sources:
        add_filter(group_node, 'None', src)


def add_filter_description(root, path):
    group_node = add_node(root, 'ItemGroup')
    filter_node = add_node(group_node, 'Filter', attrib={'Include': EXTERNAL_DIR})
    add_node(filter_node, 'UniqueIdentifier', text='{{{}}}'.format(utils.to_guid(path + 'external')))


def add_debugger_env(root, env):
    def add_env_node(root):
        text = '\n'.join('{k}={v}'.format(k=k, v=v) for k, v in env.items())
        text += '\n$(LocalDebuggerEnvironment)'
        add_node(root, 'LocalDebuggerEnvironment', text=text)
        add_node(root, 'DebuggerFlavor', text='WindowsLocalDebugger')

    for mode, platform in itertools.product(MODES, PLATFORMS):
        cond = "'$(Configuration)|$(Platform)'=='{mode}|{platform}'".format(mode=mode, platform=platform)
        property_group = add_node(root, 'PropertyGroup', attrib={'Condition': cond})
        add_env_node(property_group)
