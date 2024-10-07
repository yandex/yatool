from __future__ import absolute_import
import core.common_opts
import core.yarg

from build.build_opts import CustomFetcherOptions, SandboxAuthOptions, ToolsOptions, BuildThreadsOptions

from .style import run_style

import devtools.ya.app


class StyleOptions(core.yarg.Options):
    def __init__(self):
        self.targets = []
        self.dry_run = False
        self.check = False
        self.full_output = False
        self.stdin_filename = 'source.cpp'
        self.py2 = False
        self.force = False
        self.use_ruff = False

    @staticmethod
    def consumer():
        return [
            core.yarg.FreeArgConsumer(help='file or dir', hook=core.yarg.ExtendHook(name='targets')),
            core.yarg.ArgConsumer(
                ['--dry-run'],
                help='Print diff instead of overwriting files',
                hook=core.yarg.SetConstValueHook('dry_run', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--check'],
                help="Don't format files but return code 3 if some files would be reformatted",
                hook=core.yarg.SetConstValueHook('check', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--no-diff'],
                help="Print full file's content instead of diff. Can be used only with --dry-run",
                hook=core.yarg.SetConstValueHook('full_output', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--stdin-filename'],
                help="File name for stdin input",
                hook=core.yarg.SetValueHook('stdin_filename'),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--py2'],
                help='Use Black with Python 2 support',
                hook=core.yarg.SetConstValueHook('py2', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['-f', '--force'],
                help="Don't skip files",
                hook=core.yarg.SetConstValueHook('force', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--ruff'],
                help="Use ruff format, instead black for python files",
                hook=core.yarg.SetConstValueHook('use_ruff', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]


class FilterOptions(core.yarg.Options):
    def __init__(self):
        self.file_types = []

    @staticmethod
    def consumer():
        checks = ['py', 'cpp', 'go', 'yamake', 'cuda']

        return [
            core.yarg.ArgConsumer(
                ['--{file_type}'.format(file_type=file_type)],
                help='Process only {filetype} files'.format(filetype=file_type),
                hook=core.yarg.SetConstAppendHook('file_types', file_type),
                group=core.yarg.FILTERS_OPT_GROUP,
            )
            for file_type in checks
        ] + [
            core.yarg.ArgConsumer(
                ['--all'],
                help='Run all checks: {}'.format(', '.join(checks)),
                hook=core.yarg.SetConstValueHook('file_types', checks),
                group=core.yarg.FILTERS_OPT_GROUP,
            )
        ]


class ReportOptions(core.yarg.Options):
    def __init__(self):
        self.quiet = False

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['-q', '--quiet'],
                help="Skip warning messages",
                hook=core.yarg.SetConstValueHook('quiet', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
        ]


class StyleYaHandler(core.yarg.OptsHandler):
    description = 'Run styler'

    def __init__(self):
        core.yarg.OptsHandler.__init__(
            self,
            action=devtools.ya.app.execute(action=run_style, respawn=devtools.ya.app.RespawnType.OPTIONAL),
            description=self.description,
            opts=[
                StyleOptions(),
                ReportOptions(),
                core.common_opts.ShowHelpOptions(),
                CustomFetcherOptions(),
                SandboxAuthOptions(),
                ToolsOptions(),
                BuildThreadsOptions(build_threads=None),
                FilterOptions(),
            ],
            examples=[
                core.yarg.UsageExample(
                    '{prefix}',
                    'restyle text from <stdin>, write result to <stdout>'
                ),
                core.yarg.UsageExample(
                    '{prefix} .',
                    'restyle all files in current directory'
                ),
                core.yarg.UsageExample(
                    '{prefix} file.cpp',
                    'restyle file.cpp',
                ),
                core.yarg.UsageExample(
                    '{prefix} folder/',
                    'restyle all files in subfolders recursively'
                )
            ],
            unknown_args_as_free=False
        )
