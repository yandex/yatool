import logging

from yalibrary import platform_matcher

import devtools.ya.core.yarg
import devtools.ya.core.config

import devtools.ya.build.build_opts

from yalibrary.toolscache import toolscache_version

logger = logging.getLogger(__name__)

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
        self.dont_strip_compiler_path = False

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
                ['--dont-strip-compiler-path'],
                help='Dont strip compiler path',
                hook=devtools.ya.core.yarg.SetConstValueHook('dont_strip_compiler_path', True),
                group=COMPILATION_DATABASE_OPTS_GROUP,
            ),
            devtools.ya.core.yarg.ConfigConsumer("dont_strip_compiler_path"),
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


def _dump_compilation_database_cpp(params):
    """Dump the compilation database using the in-process C++ graph parser.

    Graph extraction and JSON serialization happen entirely in C++, avoiding
    the cost of deserializing the full graph into Python dicts.
    """
    import devtools.ya.app
    import devtools.ya.build.gen_plan2
    from devtools.ya.build.ccgraph import dump_compile_commands

    if params.file_prefixes_use_targets:
        params.file_prefixes += params.rel_targets

    tool_root = devtools.ya.core.config.tool_root(toolscache_version(params))
    # C++ side expects one of: linux, darwin, win32
    cpp_platform = platform_matcher.my_platform().split('-')[0]

    graph = devtools.ya.build.gen_plan2.ya_make_cpp_graph(params, devtools.ya.app)

    dump_compile_commands(
        graph=graph,
        source_root=params.arc_root,
        build_root=params.cmd_build_root or '',
        tool_root=tool_root,
        platform=cpp_platform,
        file_prefixes=list(params.file_prefixes),
        skip_generated=not params.files_generated,
        dont_strip_compiler_path=params.dont_strip_compiler_path,
        target_file=params.target_file,
        update=params.update,
        extra_args=list(params.cmd_extra_args),
    )


def dump_compilation_database(params):
    params.flags.setdefault('BUILD_LANGUAGES', 'CPP')
    _dump_compilation_database_cpp(params)
