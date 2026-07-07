import importlib.util
import json
import os
import sys
import types
from dataclasses import dataclass

from . import errors
from devtools.ya.core.yarg.params import Params
from devtools.ya.ide import ide_common

IDE_HELPER_FILENAME = "ide_vscode_helper.json"


@dataclass
class IDEHelperFunctions:
    pre_run: types.FunctionType | None
    post_run: types.FunctionType | None


@dataclass
class IDEHelperPrerunArgs:
    params: Params


@dataclass
class IDEHelperPostrunArgs:
    workspace_path: str
    params: Params = None


def fetch_ide_helpers(params) -> IDEHelperFunctions:
    def find_helper_files_for_target(target_path):
        files = []
        while len(target_path) >= len(params.arc_root):
            helper_file_path = os.path.join(target_path, IDE_HELPER_FILENAME)
            if os.path.isfile(helper_file_path):
                files.append(helper_file_path)
            target_path = os.path.dirname(target_path)
        return files

    all_files = set()
    for target in params.abs_targets:
        all_files.update(find_helper_files_for_target(target))

    if len(all_files) > 1:
        raise errors.YaIDEError(
            f"Multiple different '{IDE_HELPER_FILENAME}' files found for specified targets: {all_files}"
        )

    result = IDEHelperFunctions(None, None)

    if len(all_files) == 1:
        helper_path = all_files.pop()
        try:
            with open(helper_path, 'r') as f:
                data = json.load(f)
        except json.JSONDecodeError as e:
            ide_common.emit_message(f"[[bad]]Error parsing helper file {helper_path}[[rst]]: {repr(e)}")
            return result
        except FileNotFoundError:
            ide_common.emit_message(f"[[bad]]Module {helper_path} not found[[rst]]")
            return result
        result.pre_run = get_function_from_script(os.path.normpath(data.get("pre_run")), params)
        result.post_run = get_function_from_script(os.path.normpath(data.get("post_run")), params)

    return result


def get_helper_module(helper_path, arc_root):
    module_name = '.'.join(os.path.relpath(helper_path, arc_root).split(os.path.sep)[:-1])
    module_spec = importlib.util.spec_from_file_location(module_name, os.path.join(arc_root, helper_path))
    if module_spec and module_spec.loader:
        module = importlib.util.module_from_spec(module_spec)
        try:
            module_spec.loader.exec_module(module)
        except FileNotFoundError:
            ide_common.emit_message(f"[[bad]]Module {helper_path} not found[[rst]]")
            return None
        sys.modules[module_name] = module
        return module


def get_function_from_script(helper_path, params):
    if not helper_path:
        return None
    module = get_helper_module(helper_path, params.arc_root)
    if not module:
        return None
    if not hasattr(module, "run"):
        ide_common.emit_message(f"[[bad]]Helper module {helper_path} does not have function 'run'[[rst]]")
        return None
    return module.run
