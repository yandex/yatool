from __future__ import absolute_import
import exts.yjson as json
import logging
import os
import traceback
import uuid
import zipfile

import core.common_opts
import yalibrary.tools
import core.yarg

import build.build_opts
import build.genconf
import build.ya_make
import build.ymake2
import build.graph

import exts.tmp
import exts.fs
import exts.hashing
import exts.windows

import ide.ide_common

import xml.etree.ElementTree as etree
import yalibrary.platform_matcher as pm
from xml.dom import minidom

import yalibrary.graph.node as graph_node
import six
from six.moves import map

logger = logging.getLogger(__name__)

DEFAULT_MSVS_OUTPUT_DIR = 'msvs'
VALID_MSVS_VERSIONS = ('2019',)
DEFAULT_MSVS_VERSION = '2019'

SUBST_VAR_MAP = {
    # name in subst  VAR for subst   tool for default resolution
    'python2': ('BUILD_PYTHON_BIN', 'ymake_python2'),
    'python3': ('BUILD_PYTHON3_BIN', 'ymake_python3'),
}


class IdeMsvsImplSwitchOptions(core.yarg.Options):
    def __init__(self):
        self.lite = False
        self.use_clang = False
        self.use_arcadia_toolchain = False

    def consumer(self):
        return [
            core.yarg.ArgConsumer(
                ['--lite'],
                help='Lite generation mode',
                hook=core.yarg.SetConstValueHook('lite', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--use-clang'],
                help='Using local llvm-toolset',
                hook=core.yarg.SetConstValueHook('use_clang', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['-a', '--use-arcadia-toolchain'],
                help='Using arcadia toolchain',
                hook=core.yarg.SetConstValueHook('use_arcadia_toolchain', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]

    def postprocess(self):
        if self.lite and self.use_clang:
            raise core.yarg.ArgsValidatingException('--use-clang is not supported for \'ya ide msvs --lite\'')
        if self.use_arcadia_toolchain and self.use_clang:
            raise core.yarg.ArgsValidatingException('Windows toolchain for clang is not supported yet')


class IdeMsvsToolsSubst(core.yarg.Options):
    def __init__(self):
        self.tools_subst = {}

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['--tools-subst'],
                help="Customize python2 and python3 paths (tool=path)",
                hook=core.yarg.DictPutHook('tools_subst'),
                group=core.yarg.DEVELOPERS_OPT_GROUP,
            )
        ]

    def postprocess(self):
        for k in self.tools_subst:
            if k not in SUBST_VAR_MAP:
                raise core.yarg.ArgsValidatingException("You can't substitute tool {}".format(k))


MSVS_OPTS = ide.ide_common.ide_opts() + [
    core.common_opts.BeVerboseOptions(),
    core.common_opts.CustomBuildRootOptions(),
    build.build_opts.ContinueOnFailOptions(),
    build.build_opts.FlagsOptions(),
    build.build_opts.CustomFetcherOptions(),
    build.build_opts.SandboxAuthOptions(),
    build.build_opts.ToolsOptions(),
    build.build_opts.YMakeBinOptions(),
    build.build_opts.ConfigureDebugOptions(),
    ide.ide_common.IdeProjectVersionOptions(default_version=DEFAULT_MSVS_VERSION, valid_versions=VALID_MSVS_VERSIONS),
    ide.ide_common.IdeProjectInstallOptions(),
    IdeMsvsImplSwitchOptions(),
    IdeMsvsToolsSubst(),
]


class MsvsSolution(object):
    def __init__(self, params, app_ctx, info):
        self.params = params
        self.app_ctx = app_ctx
        self.info = info
        self.toolchain_params = self._gen_toolchain_params(
            params.project_version, params.use_clang, params.use_arcadia_toolchain
        )
        app_ctx.display.emit_message('Visual Studio version: [[imp]]{}[[rst]]'.format(params.project_version))
        logger.debug('Toolchain params: %s', json.dumps(self.toolchain_params, indent=2))

    def _get_tool(self, name, tool):
        if name in self.params.tools_subst:
            tool_path = self.params.tools_subst[name]
        else:
            tool_path = yalibrary.tools.tool(tool, None)

            if not tool_path.endswith('.exe'):  # XXX: hack. Think about ya.conf.json format
                logger.debug('Rename tool %s for win: %s', name, tool_path)
                tool_path += '.exe'
        return tool_path

    def _apply_tools_subst(self):
        subst_flags = {}
        for name, (var, tool) in six.iteritems(SUBST_VAR_MAP):
            if var not in self.params.flags:
                tool_path = self._get_tool(name, tool)
                if tool_path:
                    subst_flags[var] = tool_path
        return subst_flags

    def _make_tools_flags(self):
        tool_flags = {'USE_PREBUILT_TOOLS': 'no'}
        tool_flags.update(self._apply_tools_subst())
        return tool_flags

    def _make_extra_flags(self):
        extra_flags = self._make_tools_flags()
        extra_flags['DISABLE_SEPARATE_AUX_CPP'] = 'yes'
        return extra_flags

    def generate(self):
        self.ymake_conf, self.ymake_conf_digest = ide.ide_common.gen_ide_ymake_conf(
            self.params, self.toolchain_params, self._make_extra_flags()
        )
        build.ymake2.ymake_gen_proj(
            ymake_bin=self.params.ymake_bin,
            be_verbose=self.params.be_verbose,
            custom_build_directory=self.params.bld_dir,
            abs_targets=self.params.abs_targets,
            build_type=ide.ide_common.IDE_YMAKE_BUILD_TYPE,
            continue_on_fail=self.params.continue_on_fail,
            custom_conf=self.ymake_conf,
            no_ymake_resource=self.params.no_ymake_resource,
            ide_name='msvs{}'.format(self.params.project_version),
            ide_project_title=self.info.title,
            ide_project_dir=self.info.output_path,
            ev_listener=build.ya_make.get_print_listener(self.params, self.app_ctx.display),
        )

    @property
    def solution_path(self):
        return os.path.join(self.info.output_path, '{}.sln'.format(self.info.title))

    @staticmethod
    def _gen_toolchain_params(version, use_clang, use_arcadia_toolchain):
        # YMake-based MSVS solution generation must use specific toolchain
        toolchain_ide_marker = 'msvs{}'.format(version)
        logger.debug('Using toolchain ide marker: %s', toolchain_ide_marker)
        toolchain_params = build.genconf.gen_specific_tc_for_ide(toolchain_ide_marker)
        for unused_part in ('env', 'sandbox'):
            if unused_part in toolchain_params:
                toolchain_params.pop(unused_part)
        assert 'params' in toolchain_params
        toolchain_params['params']['ide_msvs'] = str(version)
        toolchain_params['params']['use_clang'] = use_clang
        toolchain_params['params']['use_arcadia_toolchain'] = use_arcadia_toolchain
        return toolchain_params


def should_replace_in_file(path, pattern):
    return pattern in exts.fs.read_file(path, binary=True).decode('utf-8', errors='replace')


def replace_in_file(path, pattern, repl):
    content = exts.fs.read_file(path, binary=False).decode('utf-8')
    new_content = content.replace(six.ensure_text(pattern), six.ensure_text(repl))
    exts.fs.write_file(path, new_content.encode('utf-8'), binary=False)
    return new_content != content


def should_replace_in_solution(solution_dir, pattern):
    for root, dirs, files in os.walk(solution_dir):
        for path in files:
            if path.endswith('.vcxproj'):
                if should_replace_in_file(os.path.join(root, path), pattern):
                    return True
    return False


def replace_in_solution(solution_dir, pattern, repl):
    replaced = False

    for root, dirs, files in os.walk(solution_dir):
        for path in files:
            if path.endswith('.vcxproj'):
                replaced = replace_in_file(os.path.join(root, path), pattern, repl)

    return replaced


def fix_scarab(solution_dir, build_root, classpath, manifest_jar_filename):
    scarab_dir = os.path.join(solution_dir, 'Misc', 'ScarabClasspath')
    exts.fs.create_dirs(scarab_dir)

    msvs_classpath = []
    for path in sorted(set(classpath)):
        if path.startswith(build_root):
            relpath = os.path.relpath(path, build_root)
            destpath = os.path.join(scarab_dir, relpath)

            if not os.path.exists(destpath):
                exts.fs.create_dirs(os.path.dirname(destpath))
                exts.fs.replace_file(path, destpath)

            msvs_classpath.append(relpath)

        else:
            msvs_classpath.append('file:/' + path.lstrip('/').lstrip('\\'))

    msvs_classpath = ' '.join(msvs_classpath)

    manifest_jar = os.path.join(scarab_dir, manifest_jar_filename)
    lines = []

    while msvs_classpath:
        lines.append(msvs_classpath[:60])
        msvs_classpath = msvs_classpath[60:]

    with zipfile.ZipFile(manifest_jar, 'w') as zf:
        if lines:
            zf.writestr('META-INF/MANIFEST.MF', 'Manifest-Version: 1.0\nClass-Path: \n ' + '\n '.join(lines) + ' \n\n')

    return manifest_jar


def fix_msvs_solution(solution_dir, opts, app_ctx):
    make_opts = core.yarg.merge_opts(build.build_opts.ya_make_options()).params()
    make_opts.__dict__.update(opts.__dict__)
    make_opts.build_threads = 1
    make_opts.create_symlinks = False
    make_opts.continue_on_fail = True

    try:
        graph, _, _, _, _ = build.graph.build_graph_and_tests(
            make_opts, check=True, ev_listener=lambda x: None, display=getattr(app_ctx, 'display', None)
        )

    except Exception:
        logger.debug('Exception while graph generation')
        logger.debug(traceback.format_exc())
        return

    keys_to_replace = set()

    for res in graph.get('conf', {}).get('resources', []):
        keys_to_replace.add('$(' + res['pattern'] + ')')

    for node in graph['graph']:
        keys_to_replace |= set(node.get('scarab', {}).keys())

    keys_to_replace = sorted(k for k in keys_to_replace if should_replace_in_solution(solution_dir, k))

    if not keys_to_replace:
        return

    result = []

    def repl_func(pat):
        return lambda rmap: replace_in_solution(solution_dir, pat, rmap.fix(pat))

    for res in graph.get('conf', {}).get('resources', []):
        pattern = '$(' + res['pattern'] + ')'

        if pattern not in keys_to_replace:
            continue

        fix_node = graph_node.Node('', [], [], []).to_serializable()
        fix_node['uid'] = str(uuid.uuid4())
        fix_node['kv']['pc'] = 'green'
        fix_node['kv']['p'] = 'RESOLVE_PATTERN'
        fix_node['cache'] = False
        fix_node['func'] = repl_func(pattern)
        fix_node['inputs'] = [pattern]

        graph['graph'].append(fix_node)
        result.append(fix_node['uid'])

    def repl_scarab_func(pattern, classpath):
        manifest_jar_filename = str(exts.hashing.fast_hash(pattern)) + '.jar'

        def f(rmap):
            mj = fix_scarab(
                solution_dir, rmap.fix('$(BUILD_ROOT)'), list(map(rmap.fix, classpath)), manifest_jar_filename
            )

            if mj.startswith(solution_dir):
                mj = '$(SolutionDir)' + os.path.relpath(mj, solution_dir)

            mj = os.path.normpath(mj).replace('/', '\\')

            replace_in_solution(solution_dir, pattern, mj)

        return f

    scarab_seen = set()
    for node in graph['graph']:
        for pattern, classpath in six.iteritems(node.get('scarab', {})):
            if pattern in scarab_seen:
                continue

            scarab_seen.add(pattern)

            fix_node = graph_node.Node('', [], [], []).to_serializable()
            fix_node['uid'] = str(uuid.uuid4())
            fix_node['kv']['pc'] = 'green'
            fix_node['kv']['p'] = 'RESOLVE_PATTERN'
            fix_node['cache'] = False
            fix_node['func'] = repl_scarab_func(pattern, classpath)
            fix_node['inputs'] = [pattern]
            fix_node['deps'] = node['deps']

            graph['graph'].append(fix_node)
            result.append(fix_node['uid'])

    graph['result'] = result

    make_opts.continue_on_fail = False  # fail if tools/acceleo can't be built
    builder = build.ya_make.YaMake(make_opts, app_ctx, graph=graph, tests=[])

    builder.go()


def _current_architecture():
    arch = pm.current_architecture()
    if arch == 'i686':
        return 'x86'
    if arch == 'X86_64':
        return 'x64'
    if arch == 'arm' or arch == 'arm64':
        return arch
    raise Exception('Unsupported architecture ' + arch + ' for \'ya ide msvs\'')


def _generate_properties(output_path):
    arch = _current_architecture()
    toolchain_root = yalibrary.tools.toolchain_root("c++", None, None)
    bin_path = os.path.join(toolchain_root, 'bin', 'Hostx64', arch)
    lib_path = os.path.join(toolchain_root, 'lib', arch)
    include_path = os.path.join(toolchain_root, 'include')

    return [
        ('CLToolExe', os.path.join(bin_path, 'cl.exe')),
        ('CLToolPath', bin_path),
        ('LinkToolExe', os.path.join(bin_path, 'link.exe')),
        ('LinkToolPath', bin_path),
        ('IncludePath', '{};$(IncludePath)'.format(include_path)),
        ('LibraryPath', '{};$(LibraryPath)'.format(lib_path)),
    ]


def gen_props_file(output_path):
    root = etree.Element('Project', {'xmlns': 'http://schemas.microsoft.com/developer/msbuild/2003'})
    prop_group = etree.Element("PropertyGroup")
    root.append(prop_group)
    for name, value in _generate_properties(output_path):
        prop = etree.Element(name)
        prop.text = value
        prop_group.append(prop)
    xmlstr = minidom.parseString(etree.tostring(root)).toprettyxml(indent="  ")
    file_name = os.path.join(output_path, 'Arcadia.Cpp.props')
    with open(file_name, 'w') as props_file:
        props_file.write(xmlstr)


def gen_msvs_solution(params):
    import app_ctx  # XXX

    app_ctx.display.emit_message('[[imp]]Generating Visual Studio solution[[rst]]')
    solution_info = ide.ide_common.IdeProjectInfo(params, app_ctx, default_output_name=DEFAULT_MSVS_OUTPUT_DIR)
    solution = MsvsSolution(params, app_ctx, solution_info)
    solution.generate()

    if getattr(params, 'java_fix', True):
        fix_msvs_solution(solution_info.output_path, params, app_ctx)

    if getattr(params, 'use_arcadia_toolchain', False):
        gen_props_file(solution_info.output_path)

    app_ctx.display.emit_message('[[good]]Ready. File to open: [[path]]{}[[rst]]'.format(solution.solution_path))
