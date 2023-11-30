import copy
import functools
import json
import os
import platform
import subprocess
from collections import OrderedDict

import app
import build.build_handler as bh
import build.build_opts as build_opts
import build.compilation_database as bc
import core.config
import core.yarg
import exts.asyncthread
import exts.fs as fs
import yalibrary.platform_matcher as pm
import yalibrary.tools
from yalibrary.toolscache import toolscache_version

from ide import ide_common, vscode


class VSCodeProject(object):
    app_ctx = None
    params = None
    project_root = None
    common_args = None
    codegen_cpp_dir = None
    links_dir = None
    python_wrappers_dir = None
    tool_platform = None
    is_cpp = False
    is_py3 = False
    is_go = False

    def __init__(self, app_ctx, params):
        self.app_ctx = app_ctx
        self.is_py3 = "PY3" in params.languages
        self.is_go = "GO" in params.languages
        self.is_cpp = "CPP" in params.languages

        if params.project_output:
            self.project_root = os.path.abspath(os.path.expanduser(params.project_output))
        else:
            self.project_root = os.path.abspath(os.curdir)
        if not params.allow_project_inside_arc and (
            self.project_root == params.arc_root or self.project_root.startswith(params.arc_root + os.path.sep)
        ):
            raise vscode.YaIDEError(
                "You should not create VS Code project inside Arc repository. "
                "Use \"-P=PROJECT_OUTPUT, --project-output=PROJECT_OUTPUT\" to set the project directory outside of Arc root (%s)"
                % params.arc_root
            )

        self.vscode_config_dir = os.path.join(self.project_root, ".vscode")

        flags = copy.copy(params.flags)
        ya_make_opts = core.yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True))
        extra_values = ["-DBUILD_LANGUAGES=%s" % " ".join(params.languages), "-DCONSISTENT_DEBUG=yes", "--prefetch"]
        params.ya_make_extra.extend(extra_values)
        extra_params = ya_make_opts.initialize(params.ya_make_extra)
        ya_make_opts.postprocess2(extra_params)
        params = core.yarg.merge_params(extra_params, params)
        params.hide_arm64_host_warning = True
        params.flags.update(extra_params.flags)
        if self.is_go:
            params.flags["CGO_ENABLED"] = "0"

        if params.darwin_arm64_platform:
            if params.host_platform is None:
                params.ya_make_extra.append("--host-platform=%s" % "default-darwin-arm64")
            params.host_platform = "default-darwin-arm64"
            if not params.target_platforms:
                params.ya_make_extra.append("--target-platform=%s" % "default-darwin-arm64")
            params.target_platforms = [core.common_opts.CrossCompilationOptions.make_platform("default-darwin-arm64")]

        self.tool_platform = None
        if params.host_platform:
            self.tool_platform = params.host_platform.split("-", 1)[1]
        elif params.darwin_arm64_platform:
            self.tool_platform = "darwin-arm64"

        self.common_args = (
            params.ya_make_extra + ["-j%s" % params.build_threads] + ["-D%s=%s" % (k, v) for k, v in flags.items()]
        )
        self.params = params

    def ensure_dirs(self):
        if not os.path.exists(self.project_root):
            ide_common.emit_message("Creating directory: {}".format(self.project_root))
            fs.ensure_dir(self.project_root)

        if self.is_cpp:
            self.codegen_cpp_dir = self.params.output_root or os.path.join(self.project_root, ".build")
            if not os.path.exists(self.codegen_cpp_dir):
                ide_common.emit_message("Creating directory: {}".format(self.codegen_cpp_dir))
                fs.ensure_dir(self.codegen_cpp_dir)

        if self.is_py3:
            self.links_dir = os.path.join(self.project_root, ".links")
            if not os.path.exists(self.links_dir):
                ide_common.emit_message("Creating directory: {}".format(self.links_dir))
                fs.ensure_dir(self.links_dir)
            if self.params.debug_enabled:
                self.python_wrappers_dir = os.path.join(self.project_root, "python_wrappers")
                if not os.path.exists(self.python_wrappers_dir):
                    ide_common.emit_message("Creating directory: {}".format(self.python_wrappers_dir))
                    fs.ensure_dir(self.python_wrappers_dir)

    def async_fetch_tools(self, for_platform=None):
        tools_list = []
        if self.is_py3 and self.params.black_formatter_enabled:
            tools_list.append("black")
        if self.is_cpp:
            if self.params.compile_commands_fix:
                tools_list.extend(["cc", "c++"])
            if self.params.debug_enabled and not pm.my_platform().startswith("darwin"):
                tools_list.append("gdbnew")
            if self.params.clang_format_enabled:
                tools_list.append("clang-format")
        if self.is_go:
            if not self.params.goroot:
                tools_list.append("go")
            if self.params.debug_enabled:
                tools_list.append("dlv")
            if self.params.patch_gopls:
                tools_list.append("gopls")
        return {
            name: exts.asyncthread.future(
                functools.partial(yalibrary.tools.tool, name, with_params=True, for_platform=for_platform), False
            )
            for name in tools_list
        }

    def gen_compile_commands(self, tool_fetcher):
        ide_common.emit_message("Generating compilation database")
        compile_commands_path = os.path.join(self.project_root, "compile_commands.json")
        build_params = copy.deepcopy(self.params)
        build_params.cmd_build_root = self.codegen_cpp_dir
        build_params.force_build_depends = self.params.tests_enabled
        build_params.target_file = compile_commands_path

        def gen(prms):
            return bc.gen_compilation_database(prms, self.app_ctx)

        compilation_database = app.execute(action=gen, respawn=app.RespawnType.NONE)(build_params)

        if self.params.compile_commands_fix:
            tools_replacements = [("clang++", tool_fetcher("c++")[0]), ("clang", tool_fetcher("cc")[0])]
            for item in compilation_database:
                if item["command"].startswith("clang"):
                    item["command"] = item["command"].replace(" -I", " -isystem")
                item["command"] = vscode.common.replace_prefix(item["command"], tools_replacements)

        ide_common.emit_message("Writing {}".format(compile_commands_path))
        with open(compile_commands_path, "w") as f:
            json.dump(compilation_database, f, indent=4)

    def do_codegen(self):
        build_params = copy.deepcopy(self.params)
        build_params.replace_result = True
        build_params.continue_on_fail = True
        if build_params.tests_enabled:
            build_params.flags["TRAVERSE_RECURSE_FOR_TESTS"] = "yes"

        if self.is_cpp:
            build_params.add_result = [ext for ext in vscode.consts.CODEGEN_EXTS_BY_LANG.get("CPP", [])]
            build_params.suppress_outputs = [ext for ext in vscode.consts.SUPRESS_EXTS_BY_LANG.get("CPP", [])]
            build_params.output_root = self.codegen_cpp_dir
            build_params.create_symlinks = False

            ide_common.emit_message("Running codegen for C++")
            app.execute(action=bh.do_ya_make, respawn=app.RespawnType.NONE)(build_params)

        languages = [lang for lang in build_params.languages if lang != "CPP"]
        if languages:
            build_params.add_result = [
                ext for lang in languages for ext in vscode.consts.CODEGEN_EXTS_BY_LANG.get(lang, [])
            ]
            build_params.suppress_outputs = [
                ext for lang in languages for ext in vscode.consts.SUPRESS_EXTS_BY_LANG.get(lang, [])
            ]
            build_params.output_root = self.params.output_root
            build_params.create_symlinks = True
            if self.is_go:
                build_params.flags["CGO_ENABLED"] = "0"

            ide_common.emit_message("Running codegen")
            app.execute(action=bh.do_ya_make, respawn=app.RespawnType.NONE)(build_params)

    def get_default_settings(self):
        settings = OrderedDict(
            (
                ("C_Cpp.intelliSenseEngine", "disabled"),
                ("explorer.fileNesting.enabled", True),
                ("explorer.fileNesting.expand", False),
                (
                    "explorer.fileNesting.patterns",
                    {
                        "*.proto": "${capture}.pb.h, ${capture}.grpc.pb.h, ${capture}.pb.h_serialized.cpp, "
                        "${capture}.pb.cc, ${capture}.grpc.pb.cc, ${capture}.pb.go, "
                        "${capture}_pb2.py, ${capture}_pb2_grpc.py, ${capture}_pb2.pyi",
                        "*.fbs": "${capture}.fbs.h, ${capture}.iter.fbs.h, ${capture}.fbs.cpp, "
                        "${capture}.py3.fbs.pysrc, ${capture}.fbs.gosrc",
                    },
                ),
                ("forbeslindesay-taskrunner.separator", ": "),
                ("git.mergeEditor", False),
                ("go.testExplorer.enable", False),
                ("go.toolsManagement.autoUpdate", False),
                ("go.toolsManagement.checkForUpdates", "off"),
                ("npm.autoDetect", "off"),
                ("python.analysis.autoSearchPaths", False),
                ("python.analysis.diagnosticMode", "openFilesOnly"),
                ("python.analysis.enablePytestSupport", False),
                ("python.analysis.indexing", False),
                ("python.languageServer", "Pylance"),
                ("python.testing.autoTestDiscoverOnSaveEnabled", False),
                ("search.followSymlinks", False),
                ("task.autoDetect", "off"),
                ("typescript.suggest.autoImports", False),
                ("typescript.tsc.autoDetect", "off"),
                ("vsicons.projectDetection.disableDetect", True),
            )
        )
        if self.is_cpp:
            settings["clangd.arguments"] = [
                "--background-index",
                "--compile-commands-dir={}".format(self.project_root),
                "--header-insertion=never",
                "--log=info",
                "--pretty",
                "-j=%s" % self.params.build_threads,
            ] + self.params.clangd_extra_args
            settings["clangd.checkUpdates"] = True

        if self.is_py3:
            settings["python.analysis.indexing"] = True
            settings["python.analysis.persistAllIndices"] = True

        if self.is_go:
            settings.update(
                (
                    ("go.logging.level", "verbose"),
                    (
                        "go.toolsEnvVars",
                        {
                            "CGO_ENABLED": "0",
                            "GOFLAGS": "-mod=vendor",
                            "GOPRIVATE": "*.yandex-team.ru,*.yandexcloud.net",
                        },
                    ),
                    (
                        "gopls",
                        OrderedDict(
                            (
                                (
                                    "build.env",
                                    {
                                        "CGO_ENABLED": "0",
                                        "GOFLAGS": "-mod=vendor",
                                        "GOPRIVATE": "*.yandex-team.ru,*.yandexcloud.net",
                                    },
                                ),
                                ("formatting.local", "a.yandex-team.ru"),
                                (
                                    "ui.codelenses",
                                    {
                                        "regenerate_cgo": False,
                                        "generate": False,
                                    },
                                ),
                                ("ui.navigation.importShortcut", "Definition"),
                                ("ui.semanticTokens", True),
                                ("verboseOutput", True),
                            )
                        ),
                    ),
                )
            )
        else:
            settings["go.useLanguageServer"] = False

        return settings

    def gen_workspace(self):
        self.ensure_dirs()
        workspace = OrderedDict(
            (
                (
                    "extensions",
                    OrderedDict((("recommendations", vscode.workspace.get_recommended_extensions(self.params)),)),
                ),
            )
        )
        if self.params.use_arcadia_root:
            workspace["folders"] = [{"path": self.params.arc_root}]
        else:
            workspace["folders"] = [
                {"path": os.path.join(self.params.arc_root, target), "name": target}
                for target in self.params.rel_targets
            ]

        if self.is_cpp and self.params.add_codegen_folder:
            workspace["folders"].append(
                {
                    "path": self.codegen_cpp_dir,
                    "name": "[codegen]",
                }
            )

        workspace["settings"] = self.get_default_settings()
        workspace["settings"]["yandex.arcRoot"] = self.params.arc_root
        workspace["settings"]["yandex.toolRoot"] = core.config.tool_root(toolscache_version())

        tools_futures = self.async_fetch_tools(for_platform=self.tool_platform)

        def tool_fetcher(name):
            return tools_futures[name]()

        if self.params.codegen_enabled:
            self.do_codegen()

        if self.is_cpp:
            workspace["settings"]["yandex.codegenRoot"] = self.codegen_cpp_dir
            self.gen_compile_commands(tool_fetcher)
        else:
            workspace["settings"]["yandex.codegenRoot"] = self.params.arc_root

        if self.is_go:
            if not self.params.goroot:
                self.params.goroot = tool_fetcher("go")[1]["toolchain_root_path"]
            workspace["settings"]["go.goroot"] = self.params.goroot

            gobin_path = os.path.join(self.params.goroot, "bin", "go.exe" if pm.my_platform() == "win32" else "go")
            if not os.path.exists(gobin_path):
                ide_common.emit_message("[[bad]]Go binary not found in:[[rst]] %s" % gobin_path)
                return

            if pm.is_darwin_arm64():
                try:
                    result = subprocess.check_output(["/usr/bin/file", gobin_path])
                    if result.strip().rsplit(" ", 1)[1] != "arm64":
                        ide_common.emit_message(
                            "[[warn]]Using X86-64 Go toolchain. Debug will not work under Rosetta.\n"
                            "Restart with \"--apple-arm-platform\" flag to use native tools[[rst]]",
                        )
                except Exception:
                    pass

        ide_common.emit_message("Collecting modules info")
        dump_module_info_res = vscode.dump.module_info(self.params)
        modules = vscode.dump.get_modules(dump_module_info_res)
        if self.is_py3:
            ide_common.emit_message("Collecting python extra paths")
            python_srcdirs = vscode.dump.get_python_srcdirs(modules)
            extra_paths = vscode.dump.collect_python_path(self.params.arc_root, self.links_dir, modules, python_srcdirs)
            python_excludes = vscode.workspace.gen_pyrights_excludes(self.params.arc_root, python_srcdirs)
            pyright_config = vscode.workspace.gen_pyrightconfig(
                self.params, python_srcdirs, extra_paths, python_excludes
            )
            for key, value in pyright_config.items():
                workspace["settings"]["python.analysis.%s" % key] = value
        run_modules = vscode.dump.filter_run_modules(modules, self.params.rel_targets)
        if self.params.debug_enabled:
            if self.params.tests_enabled:
                vscode.dump.mine_test_cwd(self.params, run_modules)
            if self.is_py3:
                vscode.dump.mine_py_main(self.params.arc_root, modules)

        ide_common.emit_message("Generating tasks")
        ya_bin_path = os.path.join(self.params.arc_root, "ya")
        default_tasks = vscode.tasks.gen_default_tasks(self.params.abs_targets, ya_bin_path, self.common_args)
        codegen_tasks = vscode.tasks.gen_codegen_tasks(
            self.params.abs_targets,
            ya_bin_path,
            self.common_args,
            self.params.languages,
            self.params.tests_enabled,
            self.codegen_cpp_dir,
        )
        tasks = vscode.tasks.gen_tasks(
            run_modules,
            self.common_args,
            self.params.arc_root,
            ya_bin_path,
            self.params.languages,
            with_prepare=self.params.debug_enabled,
        )
        workspace["tasks"] = OrderedDict(
            (
                ("version", "2.0.0"),
                ("tasks", default_tasks + codegen_tasks + tasks),
            )
        )

        workspace["launch"] = OrderedDict(
            (
                ("version", "0.2.0"),
                ("configurations", []),
            )
        )

        if self.params.debug_enabled:
            ide_common.emit_message("Generating debug configurations")
            workspace["launch"]["configurations"] = vscode.configurations.gen_debug_configurations(
                run_modules,
                self.params.arc_root,
                self.params.output_root,
                self.params.languages,
                tool_fetcher,
                self.python_wrappers_dir,
                self.params.goroot,
            )

        if self.is_cpp:
            if self.params.clang_tidy_enabled:
                ide_common.setup_tidy_config(self.params.arc_root)
                workspace["settings"]["clangd.arguments"].append("--clang-tidy")
            if self.params.clang_format_enabled:
                workspace["settings"].update(
                    vscode.workspace.gen_clang_format_settings(self.params.arc_root, tool_fetcher)
                )
        if self.is_py3 and self.params.black_formatter_enabled:
            workspace["settings"].update(
                vscode.workspace.gen_black_settings(
                    self.params.arc_root, self.params.rel_targets, python_srcdirs, tool_fetcher
                )
            )
        if self.is_go:
            alt_tools = {}
            if self.params.patch_gopls:
                alt_tools["gopls"] = tool_fetcher("gopls")[0]
            if self.params.debug_enabled:
                alt_tools["dlv"] = tool_fetcher("dlv")[0]
            if alt_tools:
                workspace["settings"]["go.alternateTools"] = alt_tools

        workspace["settings"].update(vscode.workspace.gen_exclude_settings(self.params, modules))

        workspace_path = vscode.workspace.pick_workspace_path(self.project_root, self.params.workspace_name)
        if os.path.exists(workspace_path):
            vscode.workspace.merge_workspace(workspace, workspace_path)

        vscode.workspace.sort_tasks(workspace)
        vscode.workspace.sort_configurations(workspace)
        workspace["settings"]["yandex.codenv"] = vscode.workspace.gen_codenv_params(self.params, self.params.languages)
        ide_common.emit_message("Writing {}".format(workspace_path))
        with open(workspace_path, "w") as f:
            json.dump(workspace, f, indent=4, ensure_ascii=True)

        if os.getenv("SSH_CONNECTION"):
            ide_common.emit_message(
                "[[good]]vscode://vscode-remote/ssh-remote+{hostname}{workspace_path}?windowId=_blank[[rst]]".format(
                    hostname=platform.node(), workspace_path=workspace_path
                ),
            )

        # TODO: print finish help


def gen_vscode_workspace(params):
    if pm.my_platform() == "win32" and "CPP" in params.languages:
        ide_common.emit_message(
            "[[bad]]C++ configuration for Windows is not supported.\nIssue: https://st.yandex-team.ru/YMAKE-342[[rst]]"
        )

    # noinspection PyUnresolvedReferences
    import app_ctx  # pyright: ignore[reportMissingImports]

    project = VSCodeProject(app_ctx, params)
    project.gen_workspace()
