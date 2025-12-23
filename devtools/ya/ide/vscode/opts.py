from enum import Enum

import app_config
import devtools.ya.core.yarg


# IDE name doubles as URI scheme for ssh connection
class IDEName(Enum):
    VSCODE = "vscode"
    VSCODIUM = "vscodium"
    CURSOR = "cursor"


class VSCodeAllOptions(devtools.ya.core.yarg.Options):
    GROUP = devtools.ya.core.yarg.Group("VSCode workspace options", 0)

    def __init__(self):
        self.project_output = None
        self.workspace_name = None
        self.codegen_enabled = True
        self.debug_enabled = True
        self.tests_enabled = True
        self.skip_modules = []
        self.black_formatter_enabled = True
        self.ruff_formatter_enabled = False
        self.write_pyright_config = True
        self.python_index_enabled = True
        self.python_new_extra_paths = False
        self.build_venv = False
        self.clang_format_enabled = False
        self.clang_tidy_enabled = True
        self.use_tool_clangd = True
        self.clangd_extra_args = []
        self.clangd_index_mode = "only-targets"
        self.clangd_index_threads = 0
        self.use_arcadia_root = False
        self.files_visibility = None
        self.goroot = None
        self.patch_gopls = True
        self.gopls_index_targets = True
        self.dlv_enabled = True
        self.compile_commands_fix = True
        self.allow_project_inside_arc = False
        self.ext_py_enabled = True
        self.languages = []
        self.add_codegen_folder = False
        self.ide_name = IDEName.VSCODE

    @classmethod
    def consumer(cls):
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ["-P", "--project-output"],
                help="Custom IDE workspace output directory",
                hook=devtools.ya.core.yarg.SetValueHook("project_output"),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["-W", "--workspace-name"],
                help="Custom IDE workspace name",
                hook=devtools.ya.core.yarg.SetValueHook("workspace_name"),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--cpp"],
                help="Configure workspace for C++ language",
                hook=devtools.ya.core.yarg.SetConstAppendHook("languages", "CPP"),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--go"],
                help="Configure workspace for Go language",
                hook=devtools.ya.core.yarg.SetConstAppendHook("languages", "GO"),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--py3"],
                help="Configure workspace for Python 3 language",
                hook=devtools.ya.core.yarg.SetConstAppendHook("languages", "PY3"),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--use-arcadia-root"],
                help="Use arcadia root as workspace folder",
                hook=devtools.ya.core.yarg.SetConstValueHook("use_arcadia_root", True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--files-visibility"],
                help="Limit files visibility in VS Code Explorer/Search",
                hook=devtools.ya.core.yarg.SetValueHook(
                    "files_visibility",
                    values=("targets", "targets-and-deps", "all"),
                    default_value=lambda _: "targets-and-deps",
                ),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--goroot"],
                help="Custom GOROOT directory",
                hook=devtools.ya.core.yarg.SetValueHook("goroot"),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--clang-format"],
                help="Configure \"clang-format\" code style formatting for C++",
                hook=devtools.ya.core.yarg.SetConstValueHook("clang_format_enabled", True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-codegen"],
                help="Do not run codegen",
                hook=devtools.ya.core.yarg.SetConstValueHook("codegen_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-debug"],
                help="Do not create debug configurations",
                hook=devtools.ya.core.yarg.SetConstValueHook("debug_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-tests"],
                help="Do not configure tests",
                hook=devtools.ya.core.yarg.SetConstValueHook("tests_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-ext-py"],
                help="Disable --ext-py mode for Python tests",
                hook=devtools.ya.core.yarg.SetConstValueHook("ext_py_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-clangd-tidy"],
                help="Disable clangd-tidy linting",
                hook=devtools.ya.core.yarg.SetConstValueHook("clang_tidy_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-tool-clangd"],
                help="Do not use clangd from ya tool",
                hook=devtools.ya.core.yarg.SetConstValueHook("use_tool_clangd", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--clangd-index-mode"],
                help="Configure clangd background indexing",
                hook=devtools.ya.core.yarg.SetValueHook(
                    "clangd_index_mode",
                    values=("full", "only-targets", "disabled"),
                    default_value=lambda _: "only-targets",
                ),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--clangd-index-threads"],
                help="clangd indexing threads count",
                hook=devtools.ya.core.yarg.SetValueHook('clangd_index_threads', int),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-black-formatter"],
                help="Do not configure \"black\" code style formatting",
                hook=devtools.ya.core.yarg.SetConstValueHook("black_formatter_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--ruff-formatter"],
                help="Configure \"ruff\" code style formatting",
                hook=devtools.ya.core.yarg.SetConstValueHook("ruff_formatter_enabled", True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-gopls-fix"],
                help="Do not use patched gopls",
                hook=devtools.ya.core.yarg.SetConstValueHook("patch_gopls", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--no-gopls-index-targets'],
                help='Do not index targets with gopls',
                hook=devtools.ya.core.yarg.SetConstValueHook('gopls_index_targets', False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--no-dlv'],
                help='Do not use dlv from ya tool',
                hook=devtools.ya.core.yarg.SetConstValueHook('dlv_enabled', False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-compile-commands-fix"],
                help="Do not patch compile-commands.json",
                hook=devtools.ya.core.yarg.SetConstValueHook("compile_commands_fix", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--clangd-extra-args"],
                help="Additional arguments for clangd",
                hook=devtools.ya.core.yarg.SetAppendHook("clangd_extra_args"),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--write-pyright-config"],
                help="Write pyrightconfig.json on disk",
                hook=devtools.ya.core.yarg.SetConstValueHook("write_pyright_config", True),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.NONE,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-pyright-config"],
                help="Do not write pyrightconfig.json on disk",
                hook=devtools.ya.core.yarg.SetConstValueHook("write_pyright_config", False),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-python-index"],
                help="Do not let pylance to index whole project",
                hook=devtools.ya.core.yarg.SetConstValueHook("python_index_enabled", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--python-new-extra-paths"],
                help="Use new logic for adding source paths to PYTHONPATH",
                hook=devtools.ya.core.yarg.SetConstValueHook("python_new_extra_paths", True),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--build-venv"],
                help="Build virtual environment for Python https://docs.yandex-team.ru/ya-make/usage/ya_ide/venv",
                hook=devtools.ya.core.yarg.SetConstValueHook("build_venv", True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--skip-module"],
                help="Exclude module from workspace",
                hook=devtools.ya.core.yarg.SetAppendHook("skip_modules"),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--allow-project-inside-arc"],
                help="Allow creating project inside Arc repository",
                hook=devtools.ya.core.yarg.SetConstValueHook("allow_project_inside_arc", True),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["-l", "--language"],
                help="Languages (PY3, CPP, GO). Multiple languages set by using multiple flags (default: '-l=PY3 -l=CPP')",
                hook=devtools.ya.core.yarg.SetAppendHook("languages"),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--add-codegen-folder"],
                help="Add codegen folder for C++ to workspace",
                hook=devtools.ya.core.yarg.SetConstValueHook("add_codegen_folder", True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-t', '--tests'],
                help="Generate tests configurations for debug",
                hook=devtools.ya.core.yarg.SetConstValueHook('tests_enabled', True),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.NONE,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--vscodium"],
                help="Generate workspace for VSCodium",
                hook=devtools.ya.core.yarg.SetConstValueHook("ide_name", IDEName.VSCODIUM),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--cursor"],
                help="Generate workspace for Cursor IDE",
                hook=devtools.ya.core.yarg.SetConstValueHook("ide_name", IDEName.CURSOR),
                group=cls.GROUP,
                visible=devtools.ya.core.yarg.HelpLevel.ADVANCED,
            ),
        ]

    def postprocess(self):
        if not app_config.in_house:
            self.dlv_enabled = False
            self.patch_gopls = False
            self.gopls_index_targets = False
        if self.use_arcadia_root and not self.files_visibility:
            self.files_visibility = "targets-and-deps"
        if self.files_visibility and not self.use_arcadia_root:
            self.use_arcadia_root = True

        if self.ruff_formatter_enabled:
            self.black_formatter_enabled = False

        if not self.languages:
            self.languages = ["CPP", "PY3"]
        else:
            for lang in self.languages:
                if lang not in ("CPP", "PY3", "GO"):
                    raise devtools.ya.core.yarg.ArgsValidatingException("Unsupported language: %s" % lang)

    def postprocess2(self, params):
        if params.clangd_index_threads == 0:
            params.clangd_index_threads = max(getattr(params, "build_threads", 1) // 2, 1)
