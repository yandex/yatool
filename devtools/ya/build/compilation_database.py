import exts.yjson as json
import logging
import os
import shutil
import subprocess
import sys
import tempfile

import exts.path2

import devtools.ya.core.yarg
import devtools.ya.core.config

import devtools.ya.build.build_opts
import devtools.ya.build.gen_plan2
import devtools.ya.build.graph_path

from yalibrary.fetcher import resource_fetcher
from yalibrary.toolscache import toolscache_version


logger = logging.getLogger(__name__)

SOURCE_EXTS = ('cpp', 'c', 'cc', 'cxx')
CLANG_NAMES = ('clang', 'clang++')

COMPILATION_DATABASE_OPTS_GROUP = devtools.ya.core.yarg.Group('Compilation database options', 1)


class CompilationDatabaseOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.file_prefixes = []
        self.file_prefixes_use_targets = False
        self.files_generated = True
        self.cmd_build_root = None
        self.cmd_extra_args = []
        self.target_file = None
        self.update = False
        self.dont_fix_roots = False

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--files-in'],
                help='Filter files using this source-root relative prefix',
                hook=devtools.ya.core.yarg.SetAppendHook('file_prefixes'),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--files-in-targets'],
                help='Filter files using target directories prefixes',
                hook=devtools.ya.core.yarg.SetConstValueHook('file_prefixes_use_targets', True),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--no-generated'],
                help='Filter out generated source files',
                hook=devtools.ya.core.yarg.SetConstValueHook('files_generated', False),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--cmd-build-root'],
                help='Build root to use in commands',
                hook=devtools.ya.core.yarg.SetValueHook('cmd_build_root'),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--cmd-extra-args'],
                help='Extra arguments for commands in compilation database',
                hook=devtools.ya.core.yarg.SetAppendHook('cmd_extra_args'),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--output-file'],
                help='Compilation database file name',
                hook=devtools.ya.core.yarg.SetValueHook('target_file'),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--update'],
                help='Update compilation database, preserve other records',
                hook=devtools.ya.core.yarg.SetConstValueHook('update', True),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--dont-fix-roots'],
                help='Dont replace BUILD_ROOT, SOURCE_ROOT, etc. on absolute paths',
                hook=devtools.ya.core.yarg.SetConstValueHook('dont_fix_roots', True),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
        ]

    def postprocess(self):
        if self.target_file is None and self.update:
            # TODO: Raise exception when ya ide vscode is ready
            # raise devtools.ya.core.yarg.ArgsValidatingException("--update flag can't be used without --target-file option")
            logger.debug("--update flag is ignored since --target-file is not specified")
            self.update = False


COMPILATION_DATABASE_OPTS = devtools.ya.build.build_opts.ya_make_options(free_build_targets=True) + [
    CompilationDatabaseOptions()
]


def _fix_macros(s, **patterns):
    return devtools.ya.build.graph_path.resolve_graph_value(s, upper=False, **patterns)


def _log_compiler(app_ctx, compilers, compiler):
    if compiler in compilers:
        return
    compilers.add(compiler)
    if compiler not in CLANG_NAMES:
        app_ctx.display.emit_message(
            '[[warn]]Strange compiler choice to dump compilation database: {} (use clang?)[[rst]]'.format(compiler)
        )


def create_patterns(params, graph, app_ctx):
    patterns = {
        'SOURCE_ROOT': params.arc_root,
        'BUILD_ROOT': params.cmd_build_root if params.cmd_build_root else params.arc_root,
    }

    try:
        temp_dir = tempfile.mkdtemp()
        build_root = params.cmd_build_root or temp_dir
        tool_root = devtools.ya.core.config.tool_root(toolscache_version(params))
        patterns['TOOL_ROOT'] = tool_root
        for resource in graph['conf']['resources']:
            resource_desc = resource_fetcher.select_resource(resource)
            resource_uri = resource_desc['resource']
            strip_prefix = resource_desc.get("strip_prefix")
            resource_type, _ = resource_uri.split(':', 1)
            if resource_type == 'base64':
                where = resource_fetcher.fetch_base64_resource(build_root, resource_uri)
            else:
                where = resource_fetcher.fetch_resource_if_need(
                    app_ctx.legacy_sandbox_fetcher,
                    tool_root,
                    resource_uri,
                    strip_prefix=strip_prefix,
                )
            patterns[resource['pattern']] = where
    finally:
        shutil.rmtree(temp_dir)

    return patterns


def gen_compilation_database(params, app_ctx):
    import devtools.ya.app  # XXX

    if params.file_prefixes_use_targets:
        params.file_prefixes += params.rel_targets

    graph = devtools.ya.build.gen_plan2.ya_make_graph(params, devtools.ya.app, real_ya_make_opts=True)
    if params.dont_fix_roots:
        patterns = {}
    else:
        patterns = create_patterns(params, graph, app_ctx)
    compilers = set()

    source_exts = list(SOURCE_EXTS)
    if params.flags.get("CUDA_USE_CLANG"):
        source_exts.append('cu')

    def get_input_files(node):
        inputs = [path for path in node.get('inputs', ()) if any(path.endswith('.' + ext) for ext in source_exts)]
        if not inputs:
            return

        if not params.files_generated:
            inputs = [inp for inp in inputs if not devtools.ya.build.graph_path.GraphPath(inp).build]

        if params.file_prefixes:

            def filter_by_prefix(inp):
                arcadia_path = devtools.ya.build.graph_path.GraphPath(inp).strip()
                return any(exts.path2.path_startswith(arcadia_path, prefix) for prefix in params.file_prefixes)

            inputs = [inp for inp in inputs if filter_by_prefix(inp)]
        return inputs

    def get_command_and_file(node):
        input_files = get_input_files(node)
        if not input_files:
            return

        cmds = node['cmds']
        if len(cmds) != 1:
            return
        cmd_args = cmds[0]['cmd_args']
        if not cmd_args:
            return

        cmd_args_set = set(cmd_args)
        files = [inp for inp in input_files if inp in cmd_args_set]
        if len(files) != 1:
            return

        # Remove args related to python wrapper
        WRAPCC_ARGS_END = '--wrapcc-end'
        for i, arg in enumerate(cmd_args):
            if arg == WRAPCC_ARGS_END:
                cmd_args = cmd_args[i + 1 :]
                break

        cmd_args[0] = os.path.basename(cmd_args[0])
        _log_compiler(app_ctx, compilers, cmd_args[0])
        cmd_args = [_fix_macros(x, **patterns) for x in cmd_args]
        cmd_args += params.cmd_extra_args
        return subprocess.list2cmdline(cmd_args), _fix_macros(files[0], **patterns)

    def make_cdb_node(node):
        if node.get('kv', {}).get('p') not in ('CC', 'CU'):
            return None

        outputs = node.get('outputs', ())
        if len(outputs) != 1:
            return None

        output = node['outputs'][0]
        if not output.endswith('.o') and not output.endswith('.obj'):
            return None

        command_and_file = get_command_and_file(node)
        if not command_and_file:
            return None

        return {
            'command': command_and_file[0],
            'directory': params.arc_root,
            'file': command_and_file[1],
        }

    def iter_cdb_nodes():
        for node in graph['graph']:
            cdb_node = make_cdb_node(node)
            if cdb_node is not None:
                yield cdb_node

    def merge_cdb(cdb, prev_cdb):
        files = set()
        for cdb_node in cdb:
            files.add(cdb_node['file'])
            yield cdb_node
        for cdb_node in prev_cdb:
            if cdb_node['file'] not in files:
                yield cdb_node

    cdb = iter_cdb_nodes()

    if params.update:
        try:
            with open(params.target_file) as f:
                cdb = merge_cdb(cdb, json.load(f))
        except OSError:
            pass

    return sorted(cdb, key=lambda n: (n['file'], n['command']))


def dump_compilation_database(params):
    import app_ctx  # XXX

    params.flags.setdefault('BUILD_LANGUAGES', 'CPP')
    cdb = gen_compilation_database(params, app_ctx)
    if params.target_file:
        with open(params.target_file, "w") as f:
            json.dump(
                cdb,
                f,
                indent=4,
                sort_keys=True,
            )
    else:
        json.dump(
            cdb,
            sys.stdout,
            indent=4,
            sort_keys=True,
        )
