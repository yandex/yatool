import copy
import json
import os
import platform
import subprocess
import sys
from collections import OrderedDict

import termcolor

import devtools.ya.app
import devtools.ya.build.build_opts as build_opts
import devtools.ya.build.build_handler as bh
import devtools.ya.core.common_opts
import devtools.ya.core.config
import devtools.ya.core.yarg
import exts.shlex2
import yalibrary.makelists
import yalibrary.platform_matcher as pm
import yalibrary.tools
from yalibrary.toolscache import toolscache_version

from devtools.ya.ide import ide_common, vscode

CODEGEN_EXTS = [".go", ".gosrc"]
SUPPRESS_OUTPUTS = [".cgo1.go", ".res.go", "_cgo_gotypes.go", "_cgo_import.go"]
CODEGEN_TASK = "%s make --force-build-depends --replace-result --keep-going -DCGO_ENABLED=0 %s %s"
FINISH_HELP = (
    'Workspace file '
    + termcolor.colored('%s', 'green', attrs=['bold'])
    + ' is ready\n'
    + 'Code navigation and autocomplete configured for '
    + termcolor.colored('Go', 'green')
    + ' plugin: '
    + termcolor.colored('https://marketplace.visualstudio.com/items?itemName=golang.go', attrs=['bold'])
)


class VSCodeGoOptions(devtools.ya.core.yarg.Options):
    GROUP = devtools.ya.core.yarg.Group('VSCode workspace options', 0)

    def __init__(self):
        self.project_output = None
        self.workspace_name = None
        self.darwin_arm64_platform = False
        self.codegen_enabled = True
        self.patch_gopls = True
        self.goroot = None
        self.tests_enabled = False

    @classmethod
    def consumer(cls):
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['-P', '--project-output'],
                help='Custom IDE workspace output directory',
                hook=devtools.ya.core.yarg.SetValueHook('project_output'),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-W', '--workspace-name'],
                help='Custom IDE workspace name',
                hook=devtools.ya.core.yarg.SetValueHook('workspace_name'),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--no-codegen'],
                help="Do not run codegeneration",
                hook=devtools.ya.core.yarg.SetConstValueHook('codegen_enabled', False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--apple-arm-platform'],
                help='Build native Apple ARM64 binaries',
                hook=devtools.ya.core.yarg.SetConstValueHook('darwin_arm64_platform', True),
                group=cls.GROUP,
                visible=False,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--patch-gopls'],
                help='Use gopls patched for arcadia',
                hook=devtools.ya.core.yarg.SetConstValueHook('patch_gopls', True),
                group=cls.GROUP,
                visible=False,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-gopls-fix"],
                help="Do not use patched gopls",
                hook=devtools.ya.core.yarg.SetConstValueHook("patch_gopls", False),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--goroot'],
                help='Custom GOROOT directory',
                hook=devtools.ya.core.yarg.SetValueHook('goroot'),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-t', '--tests'],
                help="Generate tests configurations for debug",
                hook=devtools.ya.core.yarg.SetConstValueHook('tests_enabled', True),
                group=cls.GROUP,
            ),
        ]

    def postprocess(self):
        pass


def do_codegen(params):
    ide_common.emit_message("Running codegen")
    build_params = copy.deepcopy(params)
    build_params.add_result = list(CODEGEN_EXTS)
    build_params.suppress_outputs = list(SUPPRESS_OUTPUTS)
    build_params.replace_result = True
    build_params.force_build_depends = True
    build_params.continue_on_fail = True
    build_params.flags["CGO_ENABLED"] = "0"
    devtools.ya.app.execute(action=bh.do_ya_make, respawn=devtools.ya.app.RespawnType.NONE)(build_params)


def gen_run_configurations(params, modules, args, YA_PATH):
    args += " -d -DGO_COMPILE_FLAGS='-N -l'"

    tasks, configurations = [], []
    tests_index, program_index = 0, 0

    for name, module in sorted(modules.items(), key=lambda item: item[0]):
        if module.get('MODULE_LANG') != 'GO' or module.get('MODULE_TYPE') != 'PROGRAM':
            continue

        short_name = os.path.basename(name)
        mangle_module_type = module.get('MANGLED_MODULE_TYPE')
        if mangle_module_type != 'GO_PROGRAM':
            name = os.path.dirname(name) or name
        name = name.replace('/', '\uff0f')

        configuration = OrderedDict(
            (
                ("name", name),
                ("type", "go"),
                ("request", "launch"),
                ("mode", "exec"),
                ("program", os.path.join(params.arc_root, module['path'])),
                ("args", []),
                ("env", {}),
                ("substitutePath", [{'from': params.arc_root, 'to': '/-S'}]),
            )
        )

        if mangle_module_type == 'GO_PROGRAM':
            task_name = "Build: %s (debug)" % name
            tasks.append(
                OrderedDict(
                    (
                        ("label", task_name),
                        ("type", "shell"),
                        (
                            "command",
                            "%s make -d %s %s"
                            % (YA_PATH, args, exts.shlex2.quote(os.path.join(params.arc_root, module['module_path']))),
                        ),
                        ("group", "build"),
                        ("problemMatcher", []),
                    )
                )
            )
            configuration["cwd"] = os.path.join(params.arc_root, module['module_path'])
            configuration["presentation"] = {
                "group": "Run",
                "order": program_index,
            }
            configuration["preLaunchTask"] = task_name
            program_index += 1
        elif mangle_module_type == 'GO_TEST' and params.tests_enabled:
            prepare_task_name = "Prepare test: %s (debug)" % name
            tasks.append(
                OrderedDict(
                    (
                        ("label", prepare_task_name),
                        ("detail", module['path']),
                        ("type", "shell"),
                        (
                            "command",
                            "%s test -A --regular-tests --keep-going --test-prepare --keep-temps %s %s"
                            % (YA_PATH, args, exts.shlex2.quote(os.path.join(params.arc_root, module['module_path']))),
                        ),
                        ("group", "build"),
                    )
                )
            )
            tasks.append(
                OrderedDict(
                    (
                        ("label", "Test: %s (debug)" % name),
                        ("detail", module['path']),
                        ("type", "shell"),
                        (
                            "command",
                            "%s test -A --regular-tests %s %s"
                            % (YA_PATH, args, exts.shlex2.quote(os.path.join(params.arc_root, module['module_path']))),
                        ),
                        ("group", "test"),
                    )
                )
            )
            configuration["presentation"] = {
                "group": "Tests",
                "order": tests_index,
            }
            test_results_path = None
            if mangle_module_type == 'GO_TEST':
                test_results_path = os.path.join(params.arc_root, module['module_path'], 'test-results', short_name)
            else:
                continue
            configuration["cwd"] = module.get('TEST_CWD', test_results_path)
            configuration['env']['YA_TEST_CONTEXT_FILE'] = os.path.join(test_results_path, 'test.context')
            configuration["preLaunchTask"] = prepare_task_name
            tests_index += 1
        else:
            continue
        configurations.append(configuration)

    return tasks, configurations


def gen_vscode_workspace(params):
    ide_common.emit_message(
        "[[warn]]DEPRECATED: 'ya ide vscode-go' [[rst]]is not supported anymore. Use [[good]]'ya ide vscode --go'[[rst]] instead.\n"
        "[[c:dark-cyan]]https://docs.yandex-team.ru/ya-make/usage/ya_ide/vscode#ya-ide-vscode[[rst]]"
    )
    orig_flags = copy.copy(params.flags)
    ya_make_opts = devtools.ya.core.yarg.merge_opts(
        build_opts.ya_make_options(free_build_targets=True),
    )
    if pm.my_platform() == 'win32':
        params.ya_make_extra.append('--output=%s' % params.arc_root)
    params.ya_make_extra.append('-DBUILD_LANGUAGES=GO')
    extra_params = ya_make_opts.initialize(params.ya_make_extra)
    ya_make_opts.postprocess2(extra_params)
    params = devtools.ya.core.yarg.merge_params(extra_params, params)
    params.flags.update(extra_params.flags)
    params.flags["CGO_ENABLED"] = "0"
    params.hide_arm64_host_warning = True

    if params.darwin_arm64_platform:
        ide_common.emit_message("[[warn]]Option '--apple-arm-platform' in no longer needed[[rst]]")

    tool_platform = None
    if params.host_platform:
        tool_platform = params.host_platform.split('-', 1)[1]

    if params.goroot:
        goroot = params.goroot
    else:
        _, tool_params = yalibrary.tools.tool("go", with_params=True, for_platform=tool_platform)
        goroot = tool_params['toolchain_root_path']

    gobin_path = os.path.join(goroot, 'bin', 'go.exe' if pm.my_platform() == 'win32' else 'go')
    if not os.path.exists(gobin_path):
        ide_common.emit_message("[[bad]]ERR: Go binary not found in %s[[rst]]" % gobin_path)
        return

    if pm.is_darwin_arm64():
        try:
            result = subprocess.check_output(['/usr/bin/file', gobin_path])
            if result.strip().rsplit(' ', 1)[1] != 'arm64':
                ide_common.emit_message("[[warn]]Using X86-64 Go toolchain. Debug will not work under Rosetta.[[rst]]")
        except Exception:
            pass

    if params.project_output:
        project_root = os.path.abspath(os.path.expanduser(params.project_output))
        if not os.path.exists(project_root):
            ide_common.emit_message(f'Creating directory: {project_root}')
            os.makedirs(project_root)
    else:
        project_root = os.path.abspath(os.curdir)

    if params.codegen_enabled:
        do_codegen(params)

    TARGETS = ' '.join(exts.shlex2.quote(arg) for arg in params.abs_targets)
    common_args = params.ya_make_extra + ["-j%s" % params.build_threads] + [f"-D{k}={v}" for k, v in orig_flags.items()]
    if params.prefetch:
        common_args.append('--prefetch')
    COMMON_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in common_args)
    codegen_args = (
        common_args
        + ['--add-result=%s' % ext for ext in CODEGEN_EXTS]
        + ['--no-output-for=%s' % ext for ext in SUPPRESS_OUTPUTS]
    )
    CODEGEN_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in codegen_args)
    YA_PATH = os.path.join(params.arc_root, "ya")

    workspace = OrderedDict(
        (
            (
                "folders",
                [{"path": os.path.join(params.arc_root, target), "name": target} for target in params.rel_targets],
            ),
            (
                "extensions",
                OrderedDict(
                    (
                        (
                            "recommendations",
                            [
                                "golang.go",
                            ],
                        ),
                        ("unwantedRecommendations", ["ms-vscode.cmake-tools"]),
                    )
                ),
            ),
            (
                "settings",
                OrderedDict(
                    (
                        ("C_Cpp.intelliSenseEngine", "disabled"),
                        ("python.languageServer", "Pylance"),
                        ("python.analysis.indexing", False),
                        ("python.analysis.autoSearchPaths", False),
                        ("python.analysis.diagnosticMode", "openFilesOnly"),
                        ("search.followSymlinks", False),
                        ("git.mergeEditor", False),
                        ("npm.autoDetect", "off"),
                        ("task.autoDetect", "off"),
                        ("typescript.tsc.autoDetect", "off"),
                        ("go.goroot", goroot),
                        ("go.testExplorer.enable", False),
                        ("go.toolsManagement.autoUpdate", False),
                        ("go.toolsManagement.checkForUpdates", "off"),
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
                                        "build.arcadiaIndexDirs",
                                        params.abs_targets,
                                    ),
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
                ),
            ),
            (
                "tasks",
                OrderedDict(
                    (
                        ("version", "2.0.0"),
                        (
                            "tasks",
                            [
                                OrderedDict(
                                    (
                                        ("label", "<Codegen>"),
                                        ("type", "shell"),
                                        ("command", CODEGEN_TASK % (YA_PATH, CODEGEN_ARGS, TARGETS)),
                                        ("group", "build"),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "<Regenerate workspace>"),
                                        ("type", "shell"),
                                        (
                                            "command",
                                            YA_PATH + " " + ' '.join(exts.shlex2.quote(arg) for arg in sys.argv[1:]),
                                        ),
                                        ("options", OrderedDict((("cwd", os.path.abspath(os.curdir)),))),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "Build: ALL (debug)"),
                                        ("type", "shell"),
                                        (
                                            "command",
                                            "%s make -d  -DGO_COMPILE_FLAGS='-N -l' %s %s"
                                            % (YA_PATH, COMMON_ARGS, TARGETS),
                                        ),
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
                                        ("command", f"{YA_PATH} make -r {COMMON_ARGS} {TARGETS}"),
                                        ("group", "build"),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "Test: ALL (small)"),
                                        ("type", "shell"),
                                        ("command", f"{YA_PATH} make -t {COMMON_ARGS} {TARGETS}"),
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
                                        ("label", "Test: ALL (medium)"),
                                        ("type", "shell"),
                                        (
                                            "command",
                                            f"{YA_PATH} make -t --test-size=MEDIUM {COMMON_ARGS} {TARGETS}",
                                        ),
                                        ("group", "test"),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "Test: ALL (small + medium)"),
                                        ("type", "shell"),
                                        ("command", f"{YA_PATH} make -tt {COMMON_ARGS} {TARGETS}"),
                                        ("group", "test"),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "Test: ALL (large)"),
                                        ("type", "shell"),
                                        (
                                            "command",
                                            f"{YA_PATH} make -t --test-size=LARGE {COMMON_ARGS} {TARGETS}",
                                        ),
                                        ("group", "test"),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "Test: ALL (small + medium + large)"),
                                        ("type", "shell"),
                                        ("command", f"{YA_PATH} make -tA {COMMON_ARGS} {TARGETS}"),
                                        ("group", "test"),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ("label", "Test: ALL (restart failed)"),
                                        ("type", "shell"),
                                        ("command", f"{YA_PATH} make -tA -X {COMMON_ARGS} {TARGETS}"),
                                        ("group", "test"),
                                    )
                                ),
                            ],
                        ),
                    )
                ),
            ),
            (
                "launch",
                OrderedDict(
                    (
                        ("version", "0.2.0"),
                        (
                            "configurations",
                            [
                                OrderedDict(
                                    (
                                        ("name", "Current Package (without CGO)"),
                                        ("type", "go"),
                                        ("request", "launch"),
                                        ("mode", "auto"),
                                        ("program", "${fileDirname}"),
                                    )
                                ),
                            ],
                        ),
                    )
                ),
            ),
        )
    )

    workspace["settings"]["yandex.arcRoot"] = params.arc_root
    workspace["settings"]["yandex.toolRoot"] = devtools.ya.core.config.tool_root(toolscache_version())
    workspace["settings"]["yandex.codegenRoot"] = params.arc_root

    if params.patch_gopls:
        workspace["settings"]["go.alternateTools"] = {
            "gopls": yalibrary.tools.tool("gopls", for_platform=tool_platform),
        }

    if platform.platform().lower().startswith('linux'):
        workspace["settings"]["files.watcherExclude"] = {"**": True}

    dump_module_info_res = vscode.dump.module_info(params)
    modules = vscode.dump.get_modules(dump_module_info_res)
    run_modules = vscode.dump.filter_run_modules(modules, params.rel_targets, params.tests_enabled)
    if params.tests_enabled:
        vscode.dump.mine_test_cwd(params, run_modules)
    tasks, configurations = gen_run_configurations(params, run_modules, COMMON_ARGS, YA_PATH)
    workspace['tasks']['tasks'].extend(tasks)
    workspace['launch']['configurations'].extend(configurations)

    workspace_path = vscode.workspace.pick_workspace_path(project_root, params.workspace_name)
    if os.path.exists(workspace_path):
        vscode.workspace.merge_workspace(workspace, workspace_path)
    vscode.workspace.sort_configurations(workspace)
    workspace["settings"]["yandex.codenv"] = vscode.workspace.gen_codenv_params(params, ["go"])
    ide_common.emit_message(f'Writing {workspace_path}')
    with open(workspace_path, 'w') as f:
        json.dump(workspace, f, indent=4, ensure_ascii=True)

    ide_common.emit_message(FINISH_HELP % workspace_path)
    if os.getenv('SSH_CONNECTION'):
        ide_common.emit_message(
            'vscode://vscode-remote/ssh-remote+{hostname}{workspace_path}?windowId=_blank'.format(
                hostname=platform.node(), workspace_path=workspace_path
            )
        )
