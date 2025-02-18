import json
import os
import platform
import shutil
import sys
from collections import OrderedDict
from pathlib import Path, PurePath

import devtools.ya.test.const as const
import yalibrary.platform_matcher as pm
from devtools.ya.ide import ide_common
from . import excludes


def merge_workspace(new, workspace_path):
    backup_path = workspace_path + ".bak"
    shutil.copyfile(workspace_path, backup_path)
    ide_common.emit_message("Old workspace file backed up to[[alt1]] %s" % os.path.basename(backup_path))
    try:
        with open(workspace_path) as f:
            old = json.load(f, object_pairs_hook=OrderedDict)
    except (ValueError, TypeError) as e:
        ide_common.emit_message(
            "[[warn]]Parsing old workspace file failed[[rst]]: %s\n[[warn]]Workspace will be created as new[[rst]]"
            % str(e)
        )
        return new

    new_folders = OrderedDict((f["path"], f) for f in new["folders"])
    for folder in old.get("folders"):
        new_folders[folder["path"]] = folder
    new["folders"] = list(new_folders.values())

    new_tasks = {task["label"]: task for task in new.get("tasks", {}).get("tasks", [])}
    for task in old.get("tasks", {}).get("tasks", []):
        if task["label"] not in new_tasks:
            new_tasks[task["label"]] = task
    new["tasks"]["tasks"] = list(new_tasks.values())

    new_configurations = {conf["name"]: conf for conf in new.get("launch", {}).get("configurations", [])}
    for conf in old.get("launch", {}).get("configurations", []):
        name = conf["name"]
        if name in new_configurations:
            for key in conf:
                if key not in ("miDebuggerPath", "setupCommands", "substitutePath", "sourceMap"):
                    new_configurations[name][key] = conf[key]
        else:
            new_configurations[name] = conf
    new["launch"]["configurations"] = list(new_configurations.values())
    old_compounds = old.get("launch", {}).get("compounds")
    if old_compounds:
        new["launch"]["compounds"] = old_compounds

    for setting in old.get("settings", []):
        if setting not in new["settings"]:
            new["settings"][setting] = old["settings"][setting]

    # Drop deprecated settings
    new["settings"].pop("python.formatting.provider", None)
    new["settings"].get("gopls", {}).pop("build.expandWorkspaceToModule", None)


def sort_tasks(workspace):
    def sorting_key(t):
        isDefault = False
        group = t.get("group", {})
        if isinstance(group, dict):
            isDefault = group.get("isDefault", False)
            group = group.get("kind", "build")

        return group + ("A" if isDefault else "B") + t.get("label", "None")

    workspace["tasks"]["tasks"].sort(key=sorting_key)


def sort_configurations(workspace):
    for type_ in ("Run", "Tests"):
        configurations = [
            conf
            for conf in workspace.get("launch", {}).get("configurations", [])
            if conf.get("presentation", {}).get("group", "Run") == type_
        ]
        configurations.sort(key=lambda conf: conf["name"])
        for index, conf in enumerate(configurations, 1):
            conf["presentation"] = {"group": type_, "order": index}


def pick_workspace_path(project_root, workspace_name=None):
    if workspace_name:
        if not workspace_name.endswith(".code-workspace"):
            workspace_name = "%s.code-workspace" % workspace_name
    else:
        workspace_name = "%s.code-workspace" % os.path.basename(project_root)
        if os.path.exists(os.path.join(project_root, "workspace.code-workspace")) and not os.path.exists(
            os.path.join(project_root, workspace_name)
        ):
            workspace_name = "workspace.code-workspace"
    return os.path.join(project_root, workspace_name)


def gen_codenv_params(params, languages):
    args = ["--prefetch", "--tests", "-j%s" % params.build_threads]
    if getattr(params, "use_arcadia_root", False):
        args.extend(
            ["--use-arcadia-root", "--files-visibility=" + getattr(params, "files_visibility", "targets-and-deps")]
        )
    return {
        "languages": languages,
        "targets": params.rel_targets,
        "arguments": args,
        "autolaunch": False,
    }


def gen_exclude_settings(params, modules):
    ide_common.emit_message("Generating excludes")
    settings = {}
    if params.use_arcadia_root:
        tree = excludes.Tree()
        for path in params.rel_targets:
            tree.add_path(path)

        if not platform.platform().lower().startswith("linux"):
            watcher_excludes = OrderedDict()
            for path in tree.gen_excludes(params.arc_root, relative=True, only_dirs=True):
                watcher_excludes[path] = True
            settings["files.watcherExclude"] = watcher_excludes

        if params.files_visibility == "targets":
            target_excludes = OrderedDict()
            for path in tree.gen_excludes(params.arc_root, relative=True, only_dirs=True):
                target_excludes[path] = True
            settings["files.exclude"] = target_excludes
        else:
            deps_excludes = OrderedDict()
            for module in modules.values():
                tree.add_path(module["module_path"])
                src_dirs = module.get("SrcDirs", [])
                for srcdir in src_dirs if isinstance(src_dirs, list) else [src_dirs]:
                    tree.add_path(srcdir)
            for path in tree.gen_excludes(params.arc_root, relative=True, only_dirs=True):
                deps_excludes[path] = True
            if params.files_visibility == "targets-and-deps":
                settings["files.exclude"] = deps_excludes
            elif params.files_visibility == "all":
                settings["search.exclude"] = deps_excludes
    if platform.platform().lower().startswith("linux"):
        settings["files.watcherExclude"] = {"**": True}
    return settings


def gen_black_settings(arc_root, rel_targets, srcdirs, tool_fetcher):
    try:
        black_binary_path = tool_fetcher("black")["executable"]
    except Exception as e:
        ide_common.emit_message(f"[[warn]]Could not get \"ya tool black\"[[rst]]: {e!r}")
        return {}

    arc_root = PurePath(arc_root)

    try:
        with open(arc_root / const.DefaultLinterConfig.Python) as afile:
            black_config_file: str = json.load(afile)[const.PythonLinterName.Black]
    except Exception as e:
        ide_common.emit_message(f"[[warn]]Could not get black config path [[rst]]: {e!r}")
        return {}

    return OrderedDict(
        (
            (
                "[python]",
                {
                    "editor.defaultFormatter": "ms-python.black-formatter",
                    "editor.formatOnSaveMode": "file",
                },
            ),
            (
                "black-formatter.args",
                [
                    "--config",
                    str(arc_root / black_config_file),
                ],
            ),
            ("black-formatter.path", [black_binary_path]),
        )
    )


def gen_clang_format_settings(arc_root, tool_fetcher):
    try:
        clang_format_binary_path = tool_fetcher("clang-format")["executable"]
    except Exception as e:
        ide_common.emit_message(f"[[warn]]Could not get \"ya tool clang-format\"[[rst]]: {e!r}")
        return {}

    arc_root = Path(arc_root)

    try:
        with open(arc_root / const.DefaultLinterConfig.Cpp) as afile:
            config_file: str = json.load(afile)[const.CppLinterName.ClangFormat]
    except Exception as e:
        ide_common.emit_message(f"[[warn]]Could not get clang-format config path [[rst]]: {e!r}")
        return {}

    config_path = arc_root / config_file

    if not config_path.exists():
        ide_common.emit_message(f"[[warn]]Failed to find clang-format config[[rst]]: '{config_path}' doesn't exist")
        return {}

    target_path = arc_root / ".clang-format"

    if pm.my_platform() == "win32":
        if target_path.exists(follow_symlinks=False):
            ide_common.emit_message(
                "[[warn]]clang-format config exists at '{}' and will be replaced by '{}'[[rst]]".format(
                    target_path,
                    config_path,
                )
            )
        try:
            shutil.copyfile(config_path, target_path, follow_symlinks=False)
        except OSError as e:
            ide_common.emit_message(f"[[warn]]Failed to setup clang-format config[[rst]]: '{e.strerror}'")
            return {}
    else:
        create_symlink = False
        if not target_path.exists(follow_symlinks=False):
            create_symlink = True
        elif target_path.is_symlink():
            link_path = target_path.readlink()
            if link_path != config_path:
                ide_common.emit_message(
                    f"[[warn]]clang-format config was updated[[rst]]: '{target_path}' "
                    f"was linked to the '{link_path}', new path: '{config_path}'",
                )
                target_path.unlink()
                create_symlink = True
        else:
            ide_common.emit_message(
                f"[[warn]]Failed to create link to the clang-format config[[rst]]: '{target_path}' is not a link"
            )

        if create_symlink:
            try:
                target_path.symlink_to(config_path)
            except OSError as e:
                ide_common.emit_message(
                    f"[[warn]]Failed to create link to the clang-format config[[rst]]: '{e.strerror}'"
                )
                return {}

    return OrderedDict(
        (
            (
                "[cpp]",
                {
                    "editor.defaultFormatter": "xaver.clang-format",
                    "editor.formatOnSaveMode": "modificationsIfAvailable",
                },
            ),
            ("clang-format.executable", clang_format_binary_path),
        )
    )


def gen_pyrights_excludes(arc_root, srcdirs):
    tree = excludes.Tree()
    for dirs in srcdirs.values():
        for path in dirs:
            tree.add_path(path)
    return ["**/node_modules"] + tree.gen_excludes(arc_root, relative=True, only_dirs=True)


def gen_pyrightconfig(params, srcdirs, extraPaths, excludes):
    def _write_config(config, path):
        ide_common.emit_message(f"Writing {path}")
        with open(path, "w") as f:
            json.dump(config, f, indent=4, ensure_ascii=False)

    pyrightconfig = {
        "typeCheckingMode": "off",
        "pythonVersion": f"{sys.version_info.major}.{sys.version_info.minor}",
        "reportMissingImports": "warning",
        "reportUndefinedVariable": "error",
        "extraPaths": extraPaths,
    }

    if params.use_arcadia_root:
        pyrightconfig["include"] = params.rel_targets
        pyrightconfig["exclude"] = excludes
    if params.write_pyright_config:
        if params.use_arcadia_root:
            _write_config(pyrightconfig, os.path.join(params.arc_root, "pyrightconfig.json"))
        else:
            for target in params.rel_targets:
                _write_config(pyrightconfig, os.path.join(params.arc_root, target, "pyrightconfig.json"))
    return pyrightconfig


def get_recommended_extensions(params):
    is_mac = pm.my_platform().startswith("darwin")
    extensions = []
    if "CPP" in params.languages:
        extensions.append("llvm-vs-code-extensions.vscode-clangd")
        if is_mac or params.vscodium:
            extensions.append("vadimcn.vscode-lldb")
        else:
            extensions.append("ms-vscode.cpptools")
        if params.clang_format_enabled:
            extensions.append("xaver.clang-format")
    if "PY3" in params.languages:
        extensions.extend(
            [
                "ms-python.python",
                "ms-python.debugpy",
            ]
        )
        if params.vscodium:
            extensions.append("detachhead.basedpyright")
        else:
            extensions.append("ms-python.vscode-pylance")
        if params.black_formatter_enabled:
            extensions.append("ms-python.black-formatter")
    if "GO" in params.languages:
        extensions.append("golang.go")
    return extensions


def get_unwanted_extensions(params):
    return [
        "ms-vscode.cmake-tools",
    ]
