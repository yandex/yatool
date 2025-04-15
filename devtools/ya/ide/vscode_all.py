import copy
import functools
import hashlib
import json
import os
import platform
import re
from collections import OrderedDict

import devtools.ya.app
import devtools.ya.build.build_handler as bh
import devtools.ya.build.build_opts as build_opts
import devtools.ya.build.compilation_database as bc
import devtools.ya.core.config
import devtools.ya.core.yarg
import exts.asyncthread
import exts.fs as fs
import yalibrary.platform_matcher as pm
import yalibrary.tools
from yalibrary.toolscache import lock_resource, toolscache_version

from devtools.ya.ide import ide_common, venv, vscode
from devtools.ya.ide.vscode.opts import IDEName


class VSCodeProject:
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
        ya_make_opts = devtools.ya.core.yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True))
        extra_values = ["-DBUILD_LANGUAGES=%s" % " ".join(params.languages), "-DCONSISTENT_DEBUG=yes", "--prefetch"]
        params.ya_make_extra.extend(extra_values)
        extra_params = ya_make_opts.initialize(params.ya_make_extra)
        ya_make_opts.postprocess2(extra_params)
        params = devtools.ya.core.yarg.merge_params(extra_params, params)
        params.hide_arm64_host_warning = True
        params.flags.update(extra_params.flags)
        if self.is_go:
            params.flags["CGO_ENABLED"] = "0"

        self.tool_platform = None
        if params.host_platform:
            platform_parts = [p.to_lower() for p in params.host_platform.split("-")]
            params.tool_platform = "-".join(p for p in platform_parts if p != "default")

        self.common_args = (
            params.ya_make_extra + [f"-j{params.build_threads}"] + [f"-D{k}={v}" for k, v in flags.items()]
        )

        if pm.is_windows() and not params.output_root:
            params.output_root = os.path.join(self.project_root, ".build")

        if params.output_root:
            self.common_args.append("--output=%s" % params.output_root)
        self.params = params

    def ensure_dirs(self):
        if not os.path.exists(self.project_root):
            ide_common.emit_message(f"Creating directory: {self.project_root}")
            fs.ensure_dir(self.project_root)

        if self.params.output_root:
            if not os.path.exists(self.params.output_root):
                ide_common.emit_message(f"Creating directory: {self.params.output_root}")
                fs.ensure_dir(self.params.output_root)

        if self.is_cpp:
            self.codegen_cpp_dir = self.params.output_root or os.path.join(self.project_root, ".build")
            if not os.path.exists(self.codegen_cpp_dir):
                ide_common.emit_message(f"Creating directory: {self.codegen_cpp_dir}")
                fs.ensure_dir(self.codegen_cpp_dir)

        if self.is_py3:
            self.links_dir = os.path.join(self.project_root, ".links")
            if not os.path.exists(self.links_dir):
                ide_common.emit_message(f"Creating directory: {self.links_dir}")
                fs.ensure_dir(self.links_dir)
            if self.params.debug_enabled:
                self.python_wrappers_dir = os.path.join(self.project_root, "python_wrappers")
                if not os.path.exists(self.python_wrappers_dir):
                    ide_common.emit_message(f"Creating directory: {self.python_wrappers_dir}")
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
                tools_list.append("clang-format-18")
            if self.params.use_tool_clangd:
                tools_list.append("clangd")
        if self.is_go:
            if not self.params.goroot:
                tools_list.append("go")
            if self.params.debug_enabled and self.params.dlv_enabled:
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
        compile_commands_path = os.path.join(
            self.project_root, os.path.expanduser(self.params.target_file or "compile_commands.json")
        )
        build_params = copy.deepcopy(self.params)
        build_params.cmd_build_root = self.codegen_cpp_dir
        build_params.force_build_depends = self.params.tests_enabled
        build_params.target_file = compile_commands_path

        def gen(prms):
            return bc.gen_compilation_database(prms, self.app_ctx)

        compilation_database = devtools.ya.app.execute(action=gen, respawn=devtools.ya.app.RespawnType.NONE)(
            build_params
        )

        tools_root = devtools.ya.core.config.tool_root(toolscache_version())
        is_windows = pm.is_windows()
        if self.params.compile_commands_fix:
            tools_replacements = [
                ("clang++", tool_fetcher("c++")["executable"]),
                ("clang", tool_fetcher("cc")["executable"]),
            ]
            for item in compilation_database:
                if item["command"].startswith("clang"):
                    item["command"] = item["command"].replace(" -I", " -isystem")
                item["command"] = vscode.common.replace_prefix(item["command"], tools_replacements)
                if is_windows:
                    item["command"] = item["command"].replace("\\", "/")
            if is_windows:
                tools_root = tools_root.replace("\\", "/")

        try:
            tool_resource_regex = re.compile(fr"({tools_root.replace("\\", "\\\\")}[/\\]\d+)")
            tools_resources_set = set()
            for item in compilation_database:
                for resource in tool_resource_regex.findall(item["command"]):
                    tools_resources_set.add(resource)
            for resource in tools_resources_set:
                lock_resource(resource)
        except Exception as e:
            ide_common.emit_message(f"[[warn]]Locking resources failed {repr(e)}[[rst]]")

        ide_common.emit_message(f"Writing {compile_commands_path}")
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
            devtools.ya.app.execute(action=bh.do_ya_make, respawn=devtools.ya.app.RespawnType.NONE)(build_params)

        languages = [lang for lang in build_params.languages if lang != "CPP"]
        if languages:
            build_params.add_result = [
                ext for lang in languages for ext in vscode.consts.CODEGEN_EXTS_BY_LANG.get(lang, [])
            ]
            build_params.suppress_outputs = [
                ext for lang in languages for ext in vscode.consts.SUPRESS_EXTS_BY_LANG.get(lang, [])
            ]

            if pm.my_platform() == "win32":
                build_params.create_symlinks = False
                build_params.output_root = self.params.output_root or self.params.arc_root

            if self.is_go:
                build_params.flags["CGO_ENABLED"] = "0"

            ide_common.emit_message("Running codegen")
            devtools.ya.app.execute(action=bh.do_ya_make, respawn=devtools.ya.app.RespawnType.NONE)(build_params)

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
                        "${capture}_pb2.py, ${capture}_pb2_grpc.py, ${capture}_pb2.pyi, "
                        "${capture}_client.usrv.pb.cpp, ${capture}_client.usrv.pb.hpp, "
                        "${capture}_service.usrv.pb.cpp, ${capture}_service.usrv.pb.hpp, "
                        "${capture}.apphost.h",
                        "*.fbs": "${capture}.fbs.h, ${capture}.iter.fbs.h, ${capture}.fbs.cpp, "
                        "${capture}.py3.fbs.pysrc, ${capture}.fbs.gosrc",
                    },
                ),
                ("git.mergeEditor", False),
                ("go.testExplorer.enable", False),
                ("go.toolsManagement.autoUpdate", False),
                ("go.toolsManagement.checkForUpdates", "off"),
                ("go.useLanguageServer", False),
                ("npm.autoDetect", "off"),
                ("python.analysis.autoSearchPaths", False),
                ("python.analysis.diagnosticMode", "openFilesOnly"),
                ("python.analysis.enablePytestSupport", False),
                ("python.analysis.indexing", False),
                ("python.languageServer", "None"),
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
                "--enable-config",
                "--compile-commands-dir={}".format(
                    os.path.dirname(self.params.target_file) if self.params.target_file else self.project_root
                ),
                "--header-insertion=never",
                "--log=info",
                "--pretty",
                "-j=%s" % self.params.clangd_index_threads,
            ] + self.params.clangd_extra_args
            if self.params.clangd_index_mode == "disabled":
                settings["clangd.arguments"].append("--background-index=0")

        if self.is_py3:
            settings["python.analysis.indexing"] = self.params.python_index_enabled
            settings["python.analysis.persistAllIndices"] = True
            if self.params.ide_name == IDEName.VSCODE:
                settings["python.languageServer"] = "Pylance"
            else:
                settings["python.languageServer"] = "None"

        if self.is_go:
            settings.update(
                (
                    ("go.useLanguageServer", True),
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
            if self.params.patch_gopls and self.params.gopls_index_targets:
                settings["gopls"]["build.arcadiaIndexDirs"] = self.params.rel_targets

        return settings

    def venv_tmp_project(self):
        return os.path.join(
            self.params.rel_targets[0], '_ya_venv_%s' % hashlib.md5(self.project_root.encode('utf-8')).hexdigest()
        )

    def do_venv(self):
        venv_opts = venv.VenvOptions()
        venv_opts.venv_add_tests = self.params.tests_enabled
        venv_opts.venv_root = os.path.join(self.project_root, 'venv')
        venv_opts.venv_with_pip = False
        fs.remove_tree_safe(venv_opts.venv_root)
        venv_opts.venv_tmp_project = self.venv_tmp_project()
        venv_params = devtools.ya.core.yarg.merge_params(venv_opts.params(), copy.deepcopy(self.params))
        venv_tmp_project_dir = os.path.join(self.params.arc_root, venv_opts.venv_tmp_project)
        if os.path.exists(venv_tmp_project_dir):
            ide_common.emit_message(f'Removing existing venv temporary project: {venv_tmp_project_dir}')
            fs.remove_tree_safe(venv_tmp_project_dir)
        ide_common.emit_message(f'Generating venv: {venv_params.venv_root}')
        devtools.ya.app.execute(venv.gen_venv, respawn=devtools.ya.app.RespawnType.NONE)(venv_params)
        return os.path.join(venv_params.venv_root, 'bin', 'python')

    def gen_workspace(self):
        self.ensure_dirs()
        workspace = OrderedDict(
            (
                (
                    "extensions",
                    OrderedDict(
                        (
                            ("recommendations", vscode.workspace.get_recommended_extensions(self.params)),
                            ("unwantedRecommendations", vscode.workspace.get_unwanted_extensions(self.params)),
                        )
                    ),
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
        workspace["settings"]["yandex.toolRoot"] = devtools.ya.core.config.tool_root(toolscache_version())

        tools_futures = self.async_fetch_tools(for_platform=self.tool_platform)

        def tool_fetcher(name) -> dict:
            executable, params = tools_futures[name]()
            if pm.is_windows() and not executable.endswith('.exe'):
                executable += '.exe'
            lock_resource(params["toolchain_root_path"])
            return {
                "executable": executable,
                **params,
            }

        if self.params.codegen_enabled:
            self.do_codegen()

        if self.is_cpp:
            if self.params.use_tool_clangd:
                workspace["settings"]["clangd.path"] = tool_fetcher("clangd")["executable"]
            workspace["settings"]["yandex.codegenRoot"] = self.codegen_cpp_dir
            self.gen_compile_commands(tool_fetcher)
        else:
            workspace["settings"]["yandex.codegenRoot"] = self.params.arc_root

        if self.is_go:
            if not self.params.goroot:
                self.params.goroot = tool_fetcher("go")["toolchain_root_path"]
                gobin_path = tool_fetcher("go")["executable"]
            else:
                gobin_path = os.path.join(self.params.goroot, "bin", "go.exe" if pm.is_windows() else "go")
            workspace["settings"]["go.goroot"] = self.params.goroot

            if not os.path.exists(gobin_path):
                ide_common.emit_message("[[bad]]Go binary not found in:[[rst]] %s" % gobin_path)
                return

        ide_common.emit_message("Collecting modules info")
        dump_module_info_res = vscode.dump.module_info(self.params)
        modules = vscode.dump.get_modules(dump_module_info_res, skip_modules=self.params.skip_modules)
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
        run_modules = vscode.dump.filter_run_modules(modules, self.params.rel_targets, self.params.tests_enabled)
        if self.params.debug_enabled:
            if self.params.tests_enabled:
                vscode.dump.mine_test_cwd(self.params, run_modules)
            if self.is_py3:
                vscode.dump.mine_py_main(self.params.arc_root, modules)

        venv_args = None
        if self.is_py3:
            if self.params.build_venv:
                self.do_venv()
            venv_args = self.params.ya_make_extra + [
                '--venv-root=%s' % os.path.join(self.project_root, 'venv'),
                '--venv-tmp-project=%s' % self.venv_tmp_project(),
            ]
            if self.params.tests_enabled:
                venv_args.append('--venv-add-tests')

        ide_common.emit_message("Generating tasks")
        ya_bin_path = os.path.join(self.params.arc_root, "ya.bat" if pm.is_windows() else "ya")
        default_tasks = vscode.tasks.gen_default_tasks(self.params.abs_targets, ya_bin_path, self.common_args)
        codegen_tasks = vscode.tasks.gen_codegen_tasks(
            self.params,
            ya_bin_path,
            self.common_args,
            venv_args,
            self.codegen_cpp_dir,
        )
        tasks = vscode.tasks.gen_tasks(
            run_modules,
            self.common_args,
            self.params.arc_root,
            ya_bin_path,
            self.params.languages,
            with_prepare=self.params.debug_enabled,
            ext_py_enabled=self.params.ext_py_enabled,
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
                self.params,
                self.codegen_cpp_dir,
                tool_fetcher,
                self.python_wrappers_dir,
            )

        if self.is_cpp:
            if self.params.clang_tidy_enabled:
                ide_common.setup_tidy_config(self.params.arc_root)
                workspace["settings"]["clangd.arguments"].append("--clang-tidy")
            if self.params.clang_format_enabled:
                workspace["settings"].update(
                    vscode.workspace.gen_clang_format_settings(self.params.arc_root, tool_fetcher)
                )
            if self.params.clangd_index_mode != "full":
                with open(os.path.join(self.params.arc_root, ".clangd"), "w") as f:
                    f.write(vscode.consts.CLANGD_BG_INDEX_DISABLED)
                with open(os.path.join(self.codegen_cpp_dir, ".clangd"), "w") as f:
                    f.write(vscode.consts.CLANGD_BG_INDEX_DISABLED)
                if self.params.clangd_index_mode == "only-targets":
                    for target in self.params.rel_targets:
                        with open(os.path.join(self.params.arc_root, target, ".clangd"), "w") as f:
                            f.write(vscode.consts.CLANGD_BG_INDEX_ENABLED)
            else:
                if os.path.exists(os.path.join(self.params.arc_root, ".clangd")):
                    os.remove(os.path.join(self.params.arc_root, ".clangd"))
                if os.path.exists(os.path.join(self.codegen_cpp_dir, ".clangd")):
                    os.remove(os.path.join(self.codegen_cpp_dir, ".clangd"))

        if self.is_py3 and self.params.black_formatter_enabled:
            workspace["settings"].update(vscode.workspace.gen_black_settings(self.params.arc_root, tool_fetcher))
        if self.is_go:
            alt_tools = {}
            if self.params.patch_gopls:
                alt_tools["gopls"] = tool_fetcher("gopls")["executable"]
            if self.params.debug_enabled and self.params.dlv_enabled:
                alt_tools["dlv"] = tool_fetcher("dlv")["executable"]
            if alt_tools:
                workspace["settings"]["go.alternateTools"] = alt_tools

        workspace["settings"].update(vscode.workspace.gen_exclude_settings(self.params, modules))

        workspace_path = vscode.workspace.pick_workspace_path(self.project_root, self.params.workspace_name)
        if os.path.exists(workspace_path):
            vscode.workspace.merge_workspace(workspace, workspace_path)

        vscode.workspace.sort_tasks(workspace)
        vscode.workspace.sort_configurations(workspace)
        workspace["settings"]["yandex.codenv"] = vscode.workspace.gen_codenv_params(self.params, self.params.languages)
        ide_common.emit_message(f"Writing {workspace_path}")
        with open(workspace_path, "w") as f:
            json.dump(workspace, f, indent=4, ensure_ascii=True)

        # TODO: print finish help

        if os.getenv("SSH_CONNECTION"):
            ide_common.emit_message(
                f"[[good]]{self.params.ide_name.value}://vscode-remote/ssh-remote+{platform.node()}{workspace_path}?windowId=_blank[[rst]]"
            )


def gen_vscode_workspace(params):
    # noinspection PyUnresolvedReferences
    import app_ctx  # pyright: ignore[reportMissingImports]

    project = VSCodeProject(app_ctx, params)
    project.gen_workspace()
