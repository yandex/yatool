import devtools.ya.app
import devtools.ya.core.common_opts
import devtools.ya.core.yarg
import devtools.ya.handlers.style.styler as stlr
import devtools.ya.handlers.style.target as trgt
import devtools.ya.handlers.style.style as stl
from devtools.ya.build.build_opts import CustomFetcherOptions, SandboxAuthOptions, ToolsOptions, BuildThreadsOptions


class StyleOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.targets: list[str] = []
        self.dry_run = False
        self.check = False
        self.full_output = False
        self.stdin_filename = trgt.STDIN_FILENAME
        self.py2 = False
        self.force = False
        self.validate = False
        self.use_ruff = False
        self.use_clang_format_yt = False
        self.use_clang_format_15 = False
        self.use_clang_format_18_vanilla = False
        self.internal_enable_implicit_taxi_formatters = False

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.FreeArgConsumer(
                help='file or dir', hook=devtools.ya.core.yarg.ExtendHook(name='targets')
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--dry-run'],
                help='Print diff instead of overwriting files',
                hook=devtools.ya.core.yarg.SetConstValueHook('dry_run', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--check'],
                help="Don't format files but return code 3 if some files would be reformatted",
                hook=devtools.ya.core.yarg.SetConstValueHook('check', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--no-diff'],
                help="Print full file's content instead of diff. Can be used only with --dry-run",
                hook=devtools.ya.core.yarg.SetConstValueHook('full_output', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--stdin-filename'],
                help="File name for stdin input",
                hook=devtools.ya.core.yarg.SetValueHook('stdin_filename'),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--py2'],
                help='Use Black with Python 2 support',
                hook=devtools.ya.core.yarg.SetConstValueHook('py2', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-f', '--force'],
                help="Don't skip files",
                hook=devtools.ya.core.yarg.SetConstValueHook('force', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--validate'],
                help="Validate configs used to style targets",
                hook=devtools.ya.core.yarg.SetConstValueHook('validate', True),
                group=devtools.ya.core.yarg.ADVANCED_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--ruff'],
                help="Use ruff format, instead of black for python files",
                hook=devtools.ya.core.yarg.SetConstValueHook('use_ruff', True),
                group=devtools.ya.core.yarg.ADVANCED_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--cpp-yt'],
                help="Use custom YT clang-format for cpp files. Only works with custom linter configs and linters.make.inc mechanism.",
                hook=devtools.ya.core.yarg.SetConstValueHook('use_clang_format_yt', True),
                group=devtools.ya.core.yarg.ADVANCED_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--clang-format-15'],
                help="Use clang-format-15 for cpp files. Only works with custom linter configs and linters.make.inc mechanism.",
                hook=devtools.ya.core.yarg.SetConstValueHook('use_clang_format_15', True),
                group=devtools.ya.core.yarg.ADVANCED_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--vanilla-cf18'],
                help="Use vanilla clang-format-18 for cpp files. Only works with custom linter configs and linters.make.inc mechanism.",
                hook=devtools.ya.core.yarg.SetConstValueHook('use_clang_format_18_vanilla', True),
                group=devtools.ya.core.yarg.ADVANCED_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ConfigConsumer('internal_enable_implicit_taxi_formatters'),
        ]


class FilterOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.file_types: list[str] = []

    @staticmethod
    def consumer():
        checks = list(stlr.StylerKind)
        # temporary until stylua support for all platform is added
        checks_without_lua = [kind for kind in checks if kind != stlr.StylerKind.LUA]

        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--{file_type}'.format(file_type=file_type)],
                help='Process only {filetype} files'.format(filetype=file_type),
                hook=devtools.ya.core.yarg.SetConstAppendHook('file_types', file_type),
                group=devtools.ya.core.yarg.FILTERS_OPT_GROUP,
            )
            for file_type in checks
        ] + [
            devtools.ya.core.yarg.ArgConsumer(
                ['--all'],
                help='Run all checks: {}'.format(', '.join(checks_without_lua)),
                hook=devtools.ya.core.yarg.SetConstValueHook('file_types', checks_without_lua),
                group=devtools.ya.core.yarg.FILTERS_OPT_GROUP,
            ),
        ]


class ReportOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.quiet = False

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['-q', '--quiet'],
                help="Skip warning messages",
                hook=devtools.ya.core.yarg.SetConstValueHook('quiet', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
        ]


class StyleYaHandler(devtools.ya.core.yarg.OptsHandler):
    description = 'Run styler'

    def __init__(self):
        devtools.ya.core.yarg.OptsHandler.__init__(
            self,
            action=devtools.ya.app.execute(action=stl.run_style, respawn=devtools.ya.app.RespawnType.OPTIONAL),
            description=self.description,
            opts=[
                StyleOptions(),
                ReportOptions(),
                devtools.ya.core.common_opts.ShowHelpOptions(),
                CustomFetcherOptions(),
                SandboxAuthOptions(),
                ToolsOptions(),
                BuildThreadsOptions(build_threads=None),
                FilterOptions(),
            ],
            examples=[
                devtools.ya.core.yarg.UsageExample('{prefix}', 'restyle text from <stdin>, write result to <stdout>'),
                devtools.ya.core.yarg.UsageExample('{prefix} .', 'restyle all files in current directory'),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} file.cpp',
                    'restyle file.cpp',
                ),
                devtools.ya.core.yarg.UsageExample('{prefix} folder/', 'restyle all files in subfolders recursively'),
            ],
            unknown_args_as_free=False,
        )
