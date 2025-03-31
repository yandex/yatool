import os
import sys
from collections import OrderedDict

import exts.shlex2
import yalibrary.platform_matcher as pm

from devtools.ya.ide import ide_common
from . import common, consts


def build_task_name(name, module_lang):
    name = "Build: %s" % name
    if module_lang == "PY3":
        name += " (Release)"
    else:
        name += " (Debug)"
    return common.pretty_name(name)


def test_task_name(name, module_lang):
    name = "Test: %s" % name
    if module_lang == "PY3":
        name += " (Release)"
    else:
        name += " (Debug)"
    return common.pretty_name(name)


def prepare_task_name(name, module_lang):
    name = "Prepare test: %s" % name
    if module_lang == "PY3":
        name += " (Release)"
    else:
        name += " (Debug)"
    return common.pretty_name(name)


def gen_tasks(run_modules, common_args, arc_root, ya_bin_path, languages, with_prepare=False, ext_py_enabled=True):
    tasks = []
    common_args.append("--ignore-recurses")
    COMMON_ARGS = " ".join(exts.shlex2.quote(arg) for arg in common_args)

    for name, module in run_modules.items():
        module_type = module.get("MANGLED_MODULE_TYPE")
        module_lang = module.get("MODULE_LANG")

        if not module_type or not module_lang or module_lang not in languages:
            ide_common.emit_message("Skipped creating tasks for module \"%s\"" % name)
            continue

        abs_module_path = exts.shlex2.quote(os.path.join(arc_root, module["module_path"]))
        module_args = COMMON_ARGS
        if module_lang == "GO":
            module_args += " -DGO_COMPILE_FLAGS='-N -l'"
        if module_lang == "PY3":
            module_args += " --build=release"

        if module_type in consts.PROGRAM_MODULE_TYPES:
            tasks.append(
                OrderedDict(
                    (
                        ("label", build_task_name(name, module_lang)),
                        ("detail", module["path"]),
                        ("type", "shell"),
                        ("command", f"{ya_bin_path} make {module_args} {abs_module_path}"),
                        ("group", "build"),
                    )
                )
            )
        elif module_type in consts.TEST_MODULE_TYPES:
            ext_py_arg = "--ext-py" if ext_py_enabled and module_type == "PY3TEST_PROGRAM__from__PY3TEST" else ""
            tasks.append(
                OrderedDict(
                    (
                        ("label", test_task_name(name, module_lang)),
                        ("detail", module["path"]),
                        ("type", "shell"),
                        (
                            "command",
                            "%s test --run-all-tests --regular-tests %s %s %s"
                            % (ya_bin_path, module_args, ext_py_arg, abs_module_path),
                        ),
                        ("group", "test"),
                    )
                )
            )
            if with_prepare:
                tasks.append(
                    OrderedDict(
                        (
                            ("label", prepare_task_name(name, module_lang)),
                            ("detail", module["path"]),
                            ("type", "shell"),
                            (
                                "command",
                                "%s test --run-all-tests --regular-tests --keep-going --test-prepare --keep-temps %s %s %s"
                                % (ya_bin_path, module_args, ext_py_arg, abs_module_path),
                            ),
                            ("group", "build"),
                        )
                    )
                )
    return tasks


def gen_default_tasks(abs_targets, ya_bin_path, common_args):
    TARGETS = " ".join(exts.shlex2.quote(arg) for arg in abs_targets)
    COMMON_ARGS = " ".join(exts.shlex2.quote(arg) for arg in common_args)

    return [
        OrderedDict(
            (
                ("label", "<Regenerate workspace>"),
                ("type", "shell"),
                ("command", ya_bin_path + " " + " ".join(exts.shlex2.quote(arg) for arg in sys.argv[1:])),
                ("options", OrderedDict((("cwd", os.path.abspath(os.curdir)),))),
            )
        ),
        OrderedDict(
            (
                ("label", "Build: ALL (debug)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} make -d {COMMON_ARGS} {TARGETS}"),
                (
                    "group",
                    OrderedDict(
                        (
                            ("kind", "build"),
                            ("isDefault", True),
                        )
                    ),
                ),
            )
        ),
        OrderedDict(
            (
                ("label", "Build: ALL (release)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} make -r {COMMON_ARGS} {TARGETS}"),
                ("group", "build"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (small)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test {COMMON_ARGS} {TARGETS}"),
                (
                    "group",
                    OrderedDict(
                        (
                            ("kind", "test"),
                            ("isDefault", True),
                        )
                    ),
                ),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (style)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test --style {COMMON_ARGS} {TARGETS}"),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (medium)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test --test-size=MEDIUM {COMMON_ARGS} {TARGETS}"),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (small + medium)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test -tt {COMMON_ARGS} {TARGETS}"),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (large)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test --test-size=LARGE {COMMON_ARGS} {TARGETS}"),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (small + medium + large)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test -A {COMMON_ARGS} {TARGETS}"),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (restart failed)"),
                ("type", "shell"),
                ("command", f"{ya_bin_path} test -A -X {COMMON_ARGS} {TARGETS}"),
                ("group", "test"),
            )
        ),
    ]


def gen_codegen_tasks(params, ya_bin_path, common_args, venv_args=None, codegen_cpp_dir=None):
    TARGETS = " ".join(exts.shlex2.quote(arg) for arg in params.abs_targets)
    tasks = []
    if "CPP" in params.languages:
        codegen_args = (
            common_args
            + ["--add-result=%s" % ext for ext in consts.CODEGEN_EXTS_BY_LANG.get("CPP", [])]
            + ["--no-output-for=%s" % ext for ext in consts.SUPRESS_EXTS_BY_LANG.get("CPP", [])]
            + ["--no-src-links"]
        )
        if params.tests_enabled:
            codegen_args.append("-DTRAVERSE_RECURSE_FOR_TESTS=yes")
        CODEGEN_ARGS = " ".join(exts.shlex2.quote(arg) for arg in codegen_args)
        tasks.append(
            OrderedDict(
                (
                    ("label", "<Codegen for C++>"),
                    ("type", "shell"),
                    (
                        "command",
                        consts.CODEGEN_CPP_TASK.format(
                            args=CODEGEN_ARGS, targets=TARGETS, ya_path=ya_bin_path, output_dir=codegen_cpp_dir
                        ),
                    ),
                    ("group", "build"),
                )
            ),
        )

    if "PY3" in params.languages and venv_args:
        VENV_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in venv_args)
        tasks.append(
            OrderedDict(
                (
                    ("label", "<Rebuild venv>"),
                    ("type", "shell"),
                    ("command", f"{ya_bin_path} ide venv {VENV_ARGS} {TARGETS}"),
                    ("group", "build"),
                ),
            ),
        )

    languages = [lang for lang in params.languages if lang != "CPP"]
    if languages:
        codegen_args = (
            common_args
            + ["--add-result=%s" % ext for lang in languages for ext in consts.CODEGEN_EXTS_BY_LANG.get(lang, [])]
            + ["--no-output-for=%s" % ext for lang in languages for ext in consts.SUPRESS_EXTS_BY_LANG.get(lang, [])]
        )
        if pm.my_platform() == "win32":
            codegen_args.append("--output=%s" % params.output_root or params.arc_root)
        if params.tests_enabled:
            codegen_args.append("-DTRAVERSE_RECURSE_FOR_TESTS=yes")
        CODEGEN_ARGS = " ".join(exts.shlex2.quote(arg) for arg in codegen_args)
        tasks.append(
            OrderedDict(
                (
                    ("label", "<Codegen>"),
                    ("type", "shell"),
                    ("command", consts.CODEGEN_TASK.format(args=CODEGEN_ARGS, targets=TARGETS, ya_path=ya_bin_path)),
                    ("group", "build"),
                )
            ),
        )

    return tasks
