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
        self.black_formatter_enabled = True
        self.write_pyright_config = False
        self.clang_format_enabled = False
        self.clang_tidy_enabled = True
        self.use_arcadia_root = False
        self.files_visibility = None
        self.goroot = None
        self.patch_gopls = True
        self.compile_commands_fix = True
        self.allow_project_inside_arc = False
        self.languages = []
        self.clangd_extra_args = []
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
                help="Limit files visibility in VS Code Explorer/Search (\"targets\", \"targets-and-deps\", \"all\")",
                hook=core.yarg.SetValueHook("files_visibility", values=("targets", "targets-and-deps", "all"), default_value=lambda _: "targets-and-deps"),
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
