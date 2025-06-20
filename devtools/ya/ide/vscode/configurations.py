import copy
import os
from collections import OrderedDict

import exts.fs as fs

import yalibrary.platform_matcher as pm
from devtools.ya.ide import ide_common
from . import common, consts, dump, tasks
from .opts import IDEName


def create_python_wrapper(wrapper_dir, template, arguments):
    fs.ensure_dir(wrapper_dir)
    wrapper_path = os.path.join(wrapper_dir, "python")
    wrapper_content = template.format(**arguments)
    with open(wrapper_path, "w") as f:
        f.write(wrapper_content)
    fs.set_execute_bits(wrapper_path)
    return wrapper_path


def gen_debug_configurations(run_modules, params, codegen_cpp_dir, tool_fetcher, python_wrappers_dir):
    arc_root = params.arc_root
    use_lldb = pm.my_platform().startswith("darwin") or params.ide_name != IDEName.VSCODE
    is_win = pm.my_platform().startswith("win")
    output_dir = params.output_root or arc_root
    cpp_debug_params = None
    if "CPP" in params.languages:
        if is_win:
            cpp_debug_params = OrderedDict(
                (
                    ("type", "cppvsdbg"),
                    ("environment", []),
                    ("visualizerFile", os.path.join(arc_root, "devtools/msvs/arcadia.natvis")),
                    (
                        "sourceFileMap",
                        {
                            "/-S": arc_root,
                            "/-B": codegen_cpp_dir or output_dir,
                        },
                    ),
                )
            )
        elif use_lldb:
            cpp_debug_params = OrderedDict(
                (
                    ("type", "lldb"),
                    ("env", {}),
                    (
                        "sourceMap",
                        {
                            "/-S": arc_root,
                            "/-B": codegen_cpp_dir or output_dir,
                        },
                    ),
                )
            )
        else:
            try:
                gdb_path = tool_fetcher("gdb")["executable"]
                cpp_debug_params = OrderedDict(
                    (
                        ("type", "cppdbg"),
                        ("MIMode", "gdb"),
                        ("miDebuggerPath", gdb_path),
                        ("environment", []),
                        (
                            "setupCommands",
                            [
                                {
                                    "description": "Enable pretty-printing for gdb",
                                    "text": "-enable-pretty-printing",
                                    "ignoreFailures": True,
                                },
                                {
                                    "description": "GDB will show the full paths to all source files",
                                    "text": "set filename-display absolute",
                                    "ignoreFailures": True,
                                },
                                {
                                    "description": "When displaying a pointer to an object, identify the actual (derived) type of the object rather than the declared type, using the virtual function table. ",  # noqa
                                    "text": "set print object on",
                                    "ignoreFailures": True,
                                },
                                {
                                    "text": "set substitute-path /-S/ " + arc_root,
                                    "description": "Map source files",
                                    "ignoreFailures": True,
                                },
                                {
                                    "text": "set substitute-path /-B/ " + (codegen_cpp_dir or output_dir),
                                    "description": "Map generated files",
                                    "ignoreFailures": True,
                                },
                            ],
                        ),
                        (
                            "sourceFileMap",
                            OrderedDict(((arc_root, {"editorPath": arc_root, "useForBreakpoints": True}),)),
                        ),
                    )
                )
            except Exception as e:
                ide_common.emit_message("Unable to get \"gdb\" tool: %s.\nSkipping debug configurations." % repr(e))

    configurations = []
    for name, module in run_modules.items():
        module_type = module.get("MANGLED_MODULE_TYPE")
        module_lang = module.get("MODULE_LANG")

        if not module_type or not module_lang or module_lang not in params.languages:
            ide_common.emit_message("Skipped creating configuration for module \"%s\"" % name)
            continue

        if module_type in consts.TEST_MODULE_TYPES:
            conf_name = common.pretty_name(os.path.dirname(name) or name)
        else:
            conf_name = common.pretty_name(name)

        if module_lang == "PY3":
            wrapper_dir = os.path.join(python_wrappers_dir, os.path.basename(module["path"]))

            configuration = OrderedDict(
                (
                    ("name", conf_name),
                    ("type", "debugpy"),
                    ("request", "launch"),
                    ("args", []),
                    ("env", {"PYDEVD_USE_CYTHON": "NO"}),  # FIXME Workaround for pydevd not supporting Python 3.11
                    ("justMyCode", True),
                )
            )

            if module_type in consts.PROGRAM_MODULE_TYPES:
                configuration["python"] = create_python_wrapper(
                    wrapper_dir,
                    consts.RUN_WRAPPER_TEMPLATE,
                    {
                        "arc_root": arc_root,
                        "path": os.path.join(arc_root, module["path"]),
                    },
                )
                py_main = module.get("py_main")
                if isinstance(py_main, dump.PyMain):
                    configuration["module"] = py_main.split(":")[0]
                elif py_main:
                    configuration["program"] = os.path.join(output_dir, module["module_path"], py_main)
                else:
                    ide_common.emit_message("Cannot create run configuration for module \"%s\"" % name)
                    continue

                configuration["cwd"] = arc_root
                configuration["preLaunchTask"] = tasks.build_task_name(name, module_lang)
                configuration["presentation"] = {"group": "Run"}

                # ---- configuration without rebuild task
                configuration_no_rebuild = copy.deepcopy(configuration)
                configuration_no_rebuild["python"] = create_python_wrapper(
                    wrapper_dir + "-no-rebuild",
                    consts.RUN_WRAPPER_TEMPLATE_SOURCE,
                    {
                        "arc_root": arc_root,
                        "path": os.path.join(arc_root, module["path"]),
                    },
                )
                configuration_no_rebuild["name"] = conf_name + " (no rebuild)"
                del configuration_no_rebuild["preLaunchTask"]
                configurations.append(configuration_no_rebuild)
                # ---------------------------------------

            elif module_type in consts.TEST_MODULE_TYPES:
                configuration["python"] = create_python_wrapper(
                    wrapper_dir,
                    consts.TEST_WRAPPER_TEMPLATE,
                    {
                        "arc_root": arc_root,
                        "path": os.path.join(output_dir, module["path"]),
                        "test_context": os.path.join(
                            arc_root, module["module_path"], "test-results", "py3test", "test.context"
                        ),
                    },
                )
                configuration["module"] = "library.python.pytest.main"

                # FIXME This should be module["TEST_CWD"], but pydebug resolves relative paths of test srcs from CWD
                configuration["cwd"] = arc_root
                configuration["preLaunchTask"] = tasks.prepare_task_name(name, module_lang)
                configuration["presentation"] = {"group": "Tests"}
            else:
                ide_common.emit_message(f"Did not create run configuration for unknown module {module_type}({name})")
                continue

        elif module_lang == "CPP":
            if not cpp_debug_params:
                continue
            configuration = OrderedDict(
                (
                    ("name", conf_name),
                    ("program", os.path.join(output_dir, module["path"])),
                    ("args", []),
                    ("request", "launch"),
                )
            )
            configuration.update(cpp_debug_params)
            if module_type in consts.PROGRAM_MODULE_TYPES:
                configuration["cwd"] = os.path.join(arc_root, module["module_path"])
                configuration["preLaunchTask"] = tasks.build_task_name(name, module_lang)
                configuration["presentation"] = {"group": "Run"}
            elif module_type in consts.TEST_MODULE_TYPES:
                if module_type in ("UNITTEST", "UNITTEST_FOR", "YT_UNITTEST", "UNITTEST_WITH_CUSTOM_ENTRY_POINT"):
                    test_results_path = os.path.join(arc_root, module["module_path"], "test-results", "unittest")
                elif module_type == "GTEST":
                    test_results_path = os.path.join(arc_root, module["module_path"], "test-results", "gtest")
                elif module_type in ("BOOSTTEST", "BOOSTTEST_WITH_MAIN", "G_BENCHMARK"):
                    test_results_path = os.path.join(
                        arc_root, module["module_path"], "test-results", os.path.basename(name)
                    )
                elif module_type == "FUZZ":
                    test_results_path = os.path.join(arc_root, module["module_path"], "test-results", "fuzz")
                else:
                    continue
                configuration["cwd"] = module.get("TEST_CWD", test_results_path)
                configuration["preLaunchTask"] = tasks.prepare_task_name(name, module_lang)
                configuration["presentation"] = {"group": "Tests"}
                environment = {"YA_TEST_CONTEXT_FILE": os.path.join(test_results_path, "test.context")}
                if use_lldb:
                    configuration["env"] = environment
                else:
                    configuration["environment"] = [{"name": n, "value": m} for n, m in environment.items()]
            else:
                ide_common.emit_message(f"Did not create run configuration for unknown module {module_type}({name})")
                continue
        elif module_lang == "GO":
            if not params.goroot:
                continue
            configuration = OrderedDict(
                (
                    ("name", conf_name),
                    ("type", "go"),
                    ("request", "launch"),
                    ("mode", "exec"),
                    ("program", os.path.join(output_dir, module['path'])),
                    ("args", []),
                    ("env", {}),
                    ("substitutePath", [{'from': arc_root, 'to': '/-S'}]),
                )
            )
            if module_type in consts.PROGRAM_MODULE_TYPES:
                configuration["cwd"] = os.path.join(arc_root, module['module_path'])
                configuration["presentation"] = {"group": "Run"}
                configuration["preLaunchTask"] = tasks.build_task_name(name, module_lang)
            elif module_type in consts.TEST_MODULE_TYPES:
                configuration["presentation"] = {"group": "Tests"}
                test_results_path = os.path.join(
                    arc_root, module['module_path'], 'test-results', os.path.basename(name)
                )
                configuration["cwd"] = module.get("TEST_CWD", test_results_path)
                configuration["env"] = {"YA_TEST_CONTEXT_FILE": os.path.join(test_results_path, 'test.context')}
                configuration["preLaunchTask"] = tasks.prepare_task_name(name, module_lang)
            else:
                ide_common.emit_message(f"Did not create run configuration for unknown module {module_type}({name})")
        else:
            ide_common.emit_message(f"Did not create run configuration for unknown language {module_lang}({name})")
            continue
        configurations.append(configuration)

    return configurations
