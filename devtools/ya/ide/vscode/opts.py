import core.yarg


class VSCodeAllOptions(core.yarg.Options):
    GROUP = core.yarg.Group("VSCode workspace options", 0)

    def __init__(self):
        self.project_output = None
        self.workspace_name = None
        self.darwin_arm64_platform = False
        self.codegen_enabled = True
        self.debug_enabled = True
        self.tests_enabled = True
        self.skip_modules = []
        self.black_formatter_enabled = True
        self.write_pyright_config = True
        self.python_index_enabled = True
        self.build_venv = False
        self.clang_format_enabled = False
        self.clang_tidy_enabled = True
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
        self.languages = []
        self.add_codegen_folder = False

    @classmethod
    def consumer(cls):
        return [
            core.yarg.ArgConsumer(
                ["-P", "--project-output"],
                help="Custom IDE workspace output directory",
                hook=core.yarg.SetValueHook("project_output"),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["-W", "--workspace-name"],
                help="Custom IDE workspace name",
                hook=core.yarg.SetValueHook("workspace_name"),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--cpp"],
                help="Configure workspace for C++ language",
                hook=core.yarg.SetConstAppendHook("languages", "CPP"),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--go"],
                help="Configure workspace for Go language",
                hook=core.yarg.SetConstAppendHook("languages", "GO"),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--py3"],
                help="Configure workspace for Python 3 language",
                hook=core.yarg.SetConstAppendHook("languages", "PY3"),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--use-arcadia-root"],
                help="Use arcadia root as workspace folder",
                hook=core.yarg.SetConstValueHook("use_arcadia_root", True),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--files-visibility"],
                help="Limit files visibility in VS Code Explorer/Search",
                hook=core.yarg.SetValueHook(
                    "files_visibility",
                    values=("targets", "targets-and-deps", "all"),
                    default_value=lambda _: "targets-and-deps",
                ),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--goroot"],
                help="Custom GOROOT directory",
                hook=core.yarg.SetValueHook("goroot"),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.ADVANCED,
            ),
            core.yarg.ArgConsumer(
                ["--apple-arm-platform"],
                help="Build native Apple ARM64 binaries",
                hook=core.yarg.SetConstValueHook("darwin_arm64_platform", True),
                group=cls.GROUP,
                visible=False,
            ),
            core.yarg.ArgConsumer(
                ["--clang-format"],
                help="Configure \"clang-format\" code style formatting for C++",
                hook=core.yarg.SetConstValueHook("clang_format_enabled", True),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-codegen"],
                help="Do not run codegen",
                hook=core.yarg.SetConstValueHook("codegen_enabled", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-debug"],
                help="Do not create debug configurations",
                hook=core.yarg.SetConstValueHook("debug_enabled", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-tests"],
                help="Do not configure tests",
                hook=core.yarg.SetConstValueHook("tests_enabled", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-clangd-tidy"],
                help="Disable clangd-tidy linting",
                hook=core.yarg.SetConstValueHook("clang_tidy_enabled", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--clangd-index-mode"],
                help="Configure clangd background indexing",
                hook=core.yarg.SetValueHook(
                    "clangd_index_mode",
                    values=("full", "only-targets", "disabled"),
                    default_value=lambda _: "only-targets",
                ),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--clangd-index-threads"],
                help="clangd indexing threads count",
                hook=core.yarg.SetValueHook('clangd_index_threads', int),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-black-formatter"],
                help="Do not configure \"black\" code style formatting",
                hook=core.yarg.SetConstValueHook("black_formatter_enabled", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-gopls-fix"],
                help="Do not use patched gopls",
                hook=core.yarg.SetConstValueHook("patch_gopls", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--no-gopls-index-targets'],
                help='Do not index targets with gopls',
                hook=core.yarg.SetConstValueHook('gopls_index_targets', False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--no-dlv'],
                help='Do not use dlv from ya tool',
                hook=core.yarg.SetConstValueHook('dlv_enabled', False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--no-compile-commands-fix"],
                help="Do not patch compile-commands.json",
                hook=core.yarg.SetConstValueHook("compile_commands_fix", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--clangd-extra-args"],
                help="Additional arguments for clangd",
                hook=core.yarg.SetAppendHook("clangd_extra_args"),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--write-pyright-config"],
                help="Write pyrightconfig.json on disk",
                hook=core.yarg.SetConstValueHook("write_pyright_config", True),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.NONE,
            ),
            core.yarg.ArgConsumer(
                ["--no-pyright-config"],
                help="Do not write pyrightconfig.json on disk",
                hook=core.yarg.SetConstValueHook("write_pyright_config", False),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.ADVANCED,
            ),
            core.yarg.ArgConsumer(
                ["--no-python-index"],
                help="Do not let pylance to index whole project",
                hook=core.yarg.SetConstValueHook("python_index_enabled", False),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--build-venv"],
                help="Build virtual environment for Python https://docs.yandex-team.ru/ya-make/usage/ya_ide/venv",
                hook=core.yarg.SetConstValueHook("build_venv", True),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ["--skip-module"],
                help="Exclude module from workspace",
                hook=core.yarg.SetAppendHook("skip_modules"),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.ADVANCED,
            ),
            core.yarg.ArgConsumer(
                ["--allow-project-inside-arc"],
                help="Allow creating project inside Arc repository",
                hook=core.yarg.SetConstValueHook("allow_project_inside_arc", True),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.ADVANCED,
            ),
            core.yarg.ArgConsumer(
                ["-l", "--language"],
                help="Languages (PY3, CPP, GO). Multiple languages set by using multiple flags (default: '-l=PY3 -l=CPP')",
                hook=core.yarg.SetAppendHook("languages"),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.ADVANCED,
            ),
            core.yarg.ArgConsumer(
                ["--add-codegen-folder"],
                help="Add codegen folder for C++ to workspace",
                hook=core.yarg.SetConstValueHook("add_codegen_folder", True),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ['-t', '--tests'],
                help="Generate tests configurations for debug",
                hook=core.yarg.SetConstValueHook('tests_enabled', True),
                group=cls.GROUP,
                visible=core.yarg.HelpLevel.NONE,
            ),
        ]

    def postprocess(self):
        if self.use_arcadia_root and not self.files_visibility:
            self.files_visibility = "targets-and-deps"
        if self.files_visibility and not self.use_arcadia_root:
            self.use_arcadia_root = True

        if not self.languages:
            self.languages = ["CPP", "PY3"]
        else:
            for lang in self.languages:
                if lang not in ("CPP", "PY3", "GO"):
                    raise core.yarg.ArgsValidatingException("Unsupported language: %s" % lang)

    def postprocess2(self, params):
        if params.clangd_index_threads == 0:
            params.clangd_index_threads = max(getattr(params, "build_threads", 1) // 2, 1)
