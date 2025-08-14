import copy
import hashlib
import os
from functools import partial

import six
import termcolor

import devtools.ya.app
import devtools.ya.core.yarg
import exts.fs as fs
import yalibrary.makelists

from devtools.ya.ide import ide_common

from . import consts


def module_info(params):
    from devtools.ya.handlers import dump

    dump_params = devtools.ya.core.yarg.merge_params(
        copy.deepcopy(params),
        dump.DumpModuleInfoOptions(),
        dump.DataOptions(),
    )
    if params.tests_enabled:
        dump_params.flags["TRAVERSE_RECURSE_FOR_TESTS"] = "yes"

    return devtools.ya.app.execute(
        action=partial(dump.do_module_info, write_stdout=False),
        respawn=devtools.ya.app.RespawnType.NONE,
    )(dump_params).stdout


def removeprefix(s, prefix):
    return s[len(prefix) :] if s.startswith(prefix) else s


def shorten_module_name(module_name, rel_targets):
    if rel_targets and len(rel_targets) == 1:
        target = rel_targets[0]
        if module_name.startswith(target):
            return module_name[len(target) + 1 :] or module_name
    return module_name


def get_modules(module_info_res, rel_targets=None, skip_modules=None):
    modules = {}
    current = {}
    skip_modules = set(skip_modules or [])

    def flush():
        if "Module Dir" in current:
            module_path = current.pop("Module Dir")
            if module_path not in skip_modules and (
                not rel_targets or any(module_path.startswith(x) for x in rel_targets)
            ):
                if "path" in current:
                    module_name = shorten_module_name(current["path"], rel_targets)
                    modules[module_name] = {
                        "module_path": module_path,
                    }
                    modules[module_name].update({removeprefix(k, "Var "): v for k, v in current.items()})

        current.clear()

    for line in six.StringIO(module_info_res):
        if not line.startswith("\t"):
            flush()
        parts = line.strip().split(":", 1)
        if len(parts) == 1:
            current["path"] = removeprefix(removeprefix(parts[0], "$S/"), "$B/")
        elif len(parts) == 2 and parts[0]:
            key = parts[0].strip()
            value = parts[1].strip()
            values = [removeprefix(removeprefix(s, "$S/"), "$B/") for s in value.split(" ")]
            current[key] = values if len(values) > 1 else values[0]
    flush()

    return modules


def filter_run_modules(modules, rel_targets, tests_enabled):
    return {
        shorten_module_name(name, rel_targets): module
        for name, module in modules.items()
        if module.get("NodeType") == "Program"
        and (tests_enabled or module.get("MANGLED_MODULE_TYPE") not in consts.TEST_MODULE_TYPES)
        and any(module["module_path"].startswith(prefix) for prefix in rel_targets)
    }


def get_python_srcdirs(modules):
    srcdirs = {}
    for module in modules.values():
        module_lang = module.get("MODULE_LANG")
        if module_lang != "PY3":
            continue
        dirs = module.get("SrcDirs", [])
        if isinstance(dirs, list):
            srcdirs[module["module_path"]] = dirs
        else:
            srcdirs[module["module_path"]] = [dirs]
    return srcdirs


def mine_test_cwd(params, modules):
    def get_test_cwd_value(node):
        for child in node.children:
            if isinstance(child, yalibrary.makelists.macro_definitions.Value):
                return child.name

    def find_test_cwd(node):
        if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == "TEST_CWD":
            return get_test_cwd_value(node)
        for child in node.children:
            test_cwd = find_test_cwd(child)
            if test_cwd:
                return test_cwd

    for _, module in modules.items():
        try:
            makelist = yalibrary.makelists.ArcProject(params.arc_root, module["module_path"]).makelist()
        except Exception as e:
            ide_common.emit_message(
                termcolor.colored("Error in module \"{}\": {}".format(module["module_path"], repr(e)), "yellow")
            )
            continue

        test_cwd = find_test_cwd(makelist)
        if test_cwd:
            module["TEST_CWD"] = os.path.join(params.arc_root, test_cwd)


def collect_python_path(arc_root, links_dir, modules, srcdirs):
    source_paths = set()

    def find_py_namespace(node):
        for child in node.children:
            if isinstance(child, yalibrary.makelists.macro_definitions.PyNamespaceValue) and child.value:
                return child.value
            if (
                isinstance(child, yalibrary.makelists.macro_definitions.Macro)
                and child.name == "PY_NAMESPACE"
                and len(child.children) > 0
                and isinstance(child.children[0], yalibrary.makelists.macro_definitions.Value)
                and child.children[0].key()
            ):
                return child.children[0].key()
            namespace = find_py_namespace(child)
            if namespace:
                return namespace

    def has_srcs(node):
        if isinstance(node, yalibrary.makelists.macro_definitions.Srcs):
            return True
        return any(has_srcs(child) for child in node.children)

    def is_protobuf(node):
        if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == 'PROTO_LIBRARY':
            return True
        return any(is_protobuf(child) for child in node.children)

    def is_top_level(node):
        if isinstance(node, yalibrary.makelists.macro_definitions.SrcValue) and node.name == "TOP_LEVEL":
            return True
        return any(is_top_level(child) for child in node.children)

    def is_flatbuf(node):
        if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == "FBS_LIBRARY":
            return True
        if isinstance(node, yalibrary.makelists.macro_definitions.SrcValue) and node.name.endswith(".fbs"):
            return True
        return any(is_flatbuf(child) for child in node.children)

    def root_src_path(path, namespace):
        path_hash = "{}_{}".format(namespace, hashlib.md5((path + namespace).encode("utf-8")).hexdigest()[:8])
        name_parts = namespace.split(".") if (namespace and namespace != ".") else []
        while name_parts and path:
            name_part = name_parts.pop()
            path, path_part = os.path.split(path)
            if name_part != path_part:
                module_virtual_dir = os.path.join(links_dir, path_hash)
                base_virtual_dir = os.path.join(module_virtual_dir, *name_parts)
                fs.ensure_dir(base_virtual_dir)
                link_path = os.path.join(base_virtual_dir, name_part)
                if os.path.lexists(link_path):
                    os.unlink(link_path)
                os.symlink(os.path.join(arc_root, path, path_part), link_path)
                return module_virtual_dir
        if path:
            return os.path.join(arc_root, path)
        return arc_root

    for module_name, module in modules.items():
        module_lang = module.get("MODULE_LANG")
        if module_lang != "PY3":
            continue

        module_dir = module["module_path"]
        module_srcdirs = srcdirs.get(module_dir, [])

        try:
            makelist = yalibrary.makelists.ArcProject(arc_root, module_dir).makelist()
        except Exception as e:
            ide_common.emit_message(termcolor.colored(f"Error in module \"{module_dir}\": {repr(e)}", "yellow"))
            continue
        if not makelist:
            continue

        namespace = find_py_namespace(makelist)
        if namespace is None:
            if is_top_level(makelist) or is_flatbuf(makelist):
                namespace = "."
            elif is_protobuf(makelist):
                namespace = module_dir.replace('/', '.').replace('-', '_')
            elif has_srcs(makelist):
                namespace = "."
            else:
                namespace = module_dir.replace('/', '.')
        for src_dir in module_srcdirs:
            source_paths.add(root_src_path(src_dir, namespace))

    return sorted(source_paths)


class PyMain(str):
    pass


def mine_py_main(arc_root, modules):
    def get_py_main_value(node):
        for child in node.children:
            if isinstance(child, yalibrary.makelists.macro_definitions.Value):
                return child.name

    def get_main_value(node):
        for child in node.children:
            if isinstance(child, yalibrary.makelists.macro_definitions.PyMainValue):
                return child.value

    def find_main(node):
        if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == "PY_MAIN":
            main = get_py_main_value(node)
            if main:
                return PyMain(main)

        if isinstance(node, yalibrary.makelists.macro_definitions.PySrcs):
            main = get_main_value(node)
            if main:
                return main

        for child in node.children:
            main = find_main(child)
            if main:
                return main

    for _, module in modules.items():
        if module.get("MANGLED_MODULE_TYPE") != "PY3_BIN__from__PY3_PROGRAM":
            continue
        try:
            makelist = yalibrary.makelists.ArcProject(arc_root, module["module_path"]).makelist()
        except Exception as e:
            ide_common.emit_message(
                termcolor.colored("Error in module \"{}\": {}".format(module["module_path"], repr(e)), "yellow")
            )
            continue

        if not makelist:
            continue

        module["py_main"] = find_main(makelist) or "__main__.py"
