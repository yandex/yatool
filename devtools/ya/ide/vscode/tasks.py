import os
import sys
from collections import OrderedDict

import exts.shlex2

from ide import ide_common
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


def gen_tasks(run_modules, common_args, arc_root, ya_bin_path, languages, with_prepare=False):
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
                        ("command", "%s make %s %s" % (ya_bin_path, module_args, abs_module_path)),
                        ("group", "build"),
                    )
                )
            )
        elif module_type in consts.TEST_MODULE_TYPES:
            tasks.append(
                OrderedDict(
                    (
                        ("label", test_task_name(name, module_lang)),
                        ("detail", module["path"]),
                        ("type", "shell"),
                        (
                            "command",
                            "%s test --run-all-tests --regular-tests %s %s"
                            % (ya_bin_path, module_args, abs_module_path),
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
                                "%s test --run-all-tests --regular-tests --keep-going --test-prepare --keep-temps %s %s"
                                % (ya_bin_path, module_args, abs_module_path),
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
                ("command", "%s make -d %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
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
                ("command", "%s make -r %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "build"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (small)"),
                ("type", "shell"),
                ("command", "%s test %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
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
                ("command", "%s test --style %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (medium)"),
                ("type", "shell"),
                ("command", "%s test --test-size=MEDIUM %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (small + medium)"),
                ("type", "shell"),
                ("command", "%s test -tt %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (large)"),
                ("type", "shell"),
                ("command", "%s test --test-size=LARGE %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (small + medium + large)"),
                ("type", "shell"),
                ("command", "%s test -A %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "test"),
            )
        ),
        OrderedDict(
            (
                ("label", "Test: ALL (restart failed)"),
                ("type", "shell"),
                ("command", "%s test -A -X %s %s" % (ya_bin_path, COMMON_ARGS, TARGETS)),
                ("group", "test"),
            )
        ),
    ]


def gen_codegen_tasks(abs_targets, ya_bin_path, common_args, languages, with_tests=False, codegen_cpp_dir=None):
    TARGETS = " ".join(exts.shlex2.quote(arg) for arg in abs_targets)
    tasks = []
    if "CPP" in languages:
        codegen_args = (
            common_args
            + ["--add-result=%s" % ext for ext in consts.CODEGEN_EXTS_BY_LANG.get("CPP", [])]
            + ["--no-output-for=%s" % ext for ext in consts.SUPRESS_EXTS_BY_LANG.get("CPP", [])]
            + ["--no-src-links"]
        )
        if with_tests:
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

    languages = [lang for lang in languages if lang != "CPP"]
    if languages:
        codegen_args = (
            common_args
            + ["--add-result=%s" % ext for lang in languages for ext in consts.CODEGEN_EXTS_BY_LANG.get(lang, [])]
            + ["--no-output-for=%s" % ext for lang in languages for ext in consts.SUPRESS_EXTS_BY_LANG.get(lang, [])]
        )
        if with_tests:
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
