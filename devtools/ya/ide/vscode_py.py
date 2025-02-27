import copy
import hashlib
import json
import os
import platform
import shutil
import sys
from collections import OrderedDict

import termcolor

import devtools.ya.build.build_handler as bh
import devtools.ya.build.build_opts as build_opts
import devtools.ya.core.common_opts
import devtools.ya.core.config
import devtools.ya.core.yarg
import exts.fs as fs
import exts.shlex2
import devtools.ya.app
import devtools.ya.test.const as const
import yalibrary.makelists
import yalibrary.platform_matcher as pm
import yalibrary.tools
from yalibrary.toolscache import toolscache_version

from devtools.ya.ide import ide_common, venv, vscode

TEST_WRAPPER_TEMPLATE = """#!/bin/sh
export Y_PYTHON_ENTRY_POINT=:main
export Y_PYTHON_CLEAR_ENTRY_POINT=1
export YA_TEST_CONTEXT_FILE='{test_context}'
exec '{path}' \"$@\"
"""


class VSCodePyOptions(devtools.ya.core.yarg.Options):
    GROUP = devtools.ya.core.yarg.Group('VSCode workspace options', 0)

    def __init__(self):
        self.project_output = None
        self.workspace_name = None
        self.codegen_enabled = True
        self.use_arcadia_root = False
        self.files_visibility = False
        self.tests_enabled = False
        self.black_enabled = False
        self.venv_excluded_peerdirs = []
        self.allow_project_inside_arc = False
        self.python_index_enabled = True

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
                ['--use-arcadia-root'],
                help="Use arcadia root as workspace folder",
                hook=devtools.ya.core.yarg.SetConstValueHook('use_arcadia_root', True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--files-visibility'],
                help='Limit files visibility in VS Code Explorer/Search',
                hook=devtools.ya.core.yarg.SetValueHook(
                    'files_visibility',
                    values=("targets", "targets-and-deps", "all"),
                    default_value=lambda _: "targets-and-deps",
                ),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-t', '--tests'],
                help="Generate tests configurations for debug",
                hook=devtools.ya.core.yarg.SetConstValueHook('tests_enabled', True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--black-formatter'],
                help="Add 'black' code style formatting",
                hook=devtools.ya.core.yarg.SetConstValueHook('black_enabled', True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--venv-excluded-peerdirs'],
                help='Totally exclude specified peerdirs',
                hook=devtools.ya.core.yarg.SetAppendHook('venv_excluded_peerdirs'),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--allow-project-inside-arc'],
                help="Allow creating project inside Arc repository",
                hook=devtools.ya.core.yarg.SetConstValueHook('allow_project_inside_arc', True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ["--no-python-index"],
                help="Do not let pylance to index whole project",
                hook=devtools.ya.core.yarg.SetConstValueHook("python_index_enabled", False),
                group=cls.GROUP,
            ),
        ]

    def postprocess(self):
        if self.use_arcadia_root and not self.files_visibility:
            self.files_visibility = 'targets-and-deps'
        if self.files_visibility and not self.use_arcadia_root:
            self.use_arcadia_root = True


class PyProject:
    class PyMain(str):
        pass

    CODEGEN_EXTS = [".py", ".pyi", ".pysrc"]
    CODEGEN_TASK = "{ya_path} make --force-build-depends --replace-result --keep-going {args} {targets}"
    FINISH_HELP = (
        'Workspace file '
        + termcolor.colored('%s', 'green', attrs=['bold'])
        + ' is ready\n'
        + 'Code navigation and autocomplete configured for '
        + termcolor.colored('Python', 'green')
        + ' plugin: '
        + termcolor.colored('https://marketplace.visualstudio.com/items?itemName=ms-python.python', attrs=['bold'])
    )

    def __init__(self, params, flags):
        if params.project_output:
            self.project_root = os.path.abspath(os.path.expanduser(params.project_output))
        else:
            self.project_root = os.path.abspath(os.curdir)

        if not params.allow_project_inside_arc and (
            self.project_root == params.arc_root or self.project_root.startswith(params.arc_root + os.path.sep)
        ):
            raise vscode.YaIDEError(
                'You should not create VS Code project inside Arc repository. '
                'Use "-P=PROJECT_OUTPUT, --project-output=PROJECT_OUTPUT" to set the project directory outside of Arc root (%s)'
                % params.arc_root
            )

        self.common_args = (
            params.ya_make_extra + [f"-j{params.build_threads}"] + [f"-D{k}={v}" for k, v in flags.items()]
        )
        if params.prefetch:
            self.common_args.append('--prefetch')
        self.params = params
        self.YA_PATH = os.path.join(self.params.arc_root, "ya")

    def gen_workspace(self):
        if not os.path.exists(self.project_root):
            ide_common.emit_message(f'Creating directory: {self.project_root}')
            fs.ensure_dir(self.project_root)

        links_dir = os.path.join(self.project_root, '.links')
        if not os.path.exists(links_dir):
            ide_common.emit_message(f'Creating directory: {links_dir}')
            fs.ensure_dir(links_dir)

        python_binary_path = self.do_venv()
        if self.params.codegen_enabled:
            self.do_codegen()

        workspace = self.get_workspace_template()
        if self.params.use_arcadia_root:
            workspace["folders"] = [{"path": self.params.arc_root}]
        else:
            workspace["folders"] = [
                {"path": os.path.join(self.params.arc_root, target), "name": target}
                for target in self.params.rel_targets
            ]

        workspace["settings"]["yandex.arcRoot"] = self.params.arc_root
        workspace["settings"]["yandex.toolRoot"] = devtools.ya.core.config.tool_root(toolscache_version())
        workspace["settings"]["yandex.codegenRoot"] = self.params.arc_root

        workspace["settings"]["python.defaultInterpreterPath"] = python_binary_path

        ide_common.emit_message('Collect extraPaths')
        dump_module_info_res = vscode.dump.module_info(self.params)
        modules = vscode.dump.get_modules(dump_module_info_res)
        srcdirs = self.get_srcdirs(modules)
        extraPaths = self.mine_python_path(links_dir, srcdirs)
        workspace["settings"]["python.analysis.extraPaths"] = extraPaths
        self.write_pyrightconfig(srcdirs, extraPaths)

        ide_common.emit_message('Generating debug configurations')
        run_modules = vscode.dump.filter_run_modules(modules, self.params.rel_targets, self.params.tests_enabled)
        self.mine_py_main(run_modules)
        tasks, configurations = self.gen_run_configurations(run_modules)
        workspace['tasks']['tasks'].extend(tasks)
        workspace['launch']['configurations'].extend(configurations)

        if self.params.tests_enabled:
            ide_common.emit_message('Generating tests debug configurations')
            test_wrappers_dir = os.path.join(self.project_root, 'test_wrappers')
            if not os.path.exists(test_wrappers_dir):
                ide_common.emit_message(f'Creating directory: {test_wrappers_dir}')
                fs.ensure_dir(test_wrappers_dir)
            tasks, configurations = self.gen_test_configurations(run_modules, test_wrappers_dir)
            workspace['tasks']['tasks'].extend(tasks)
            workspace['launch']['configurations'].extend(configurations)

        if self.params.black_enabled:
            ide_common.emit_message("Generating 'black' formatter settings")
            workspace['settings'].update(self.gen_black_settings(srcdirs))

        workspace['settings'].update(vscode.workspace.gen_exclude_settings(self.params, modules))

        workspace_path = vscode.workspace.pick_workspace_path(self.project_root, self.params.workspace_name)
        if os.path.exists(workspace_path):
            vscode.workspace.merge_workspace(workspace, workspace_path)
        vscode.workspace.sort_configurations(workspace)
        workspace["settings"]["yandex.codenv"] = vscode.workspace.gen_codenv_params(self.params, ["py3"])

        ide_common.emit_message(f'Writing {workspace_path}')
        with open(workspace_path, 'w') as f:
            json.dump(workspace, f, indent=4, ensure_ascii=True)

        ide_common.emit_message(self.FINISH_HELP % workspace_path)
        if os.getenv('SSH_CONNECTION'):
            ide_common.emit_message(
                'vscode://vscode-remote/ssh-remote+{hostname}{workspace_path}?windowId=_blank'.format(
                    hostname=platform.node(), workspace_path=workspace_path
                )
            )

    def gen_pyrights_excludes(self, srcdirs):
        tree = vscode.excludes.Tree()
        for dirs in srcdirs.values():
            for path in dirs:
                tree.add_path(path)
        return ['**/node_modules'] + tree.gen_excludes(self.params.arc_root, relative=True, only_dirs=True)

    def write_pyrightconfig(self, srcdirs, extraPaths):
        def _write_config(config, path):
            ide_common.emit_message(f'Writing {path}')
            with open(path, 'w') as f:
                json.dump(config, f, indent=4, ensure_ascii=False)

        pyrightconfig = {
            "venvPath": self.project_root,
            "venv": "venv",
            "extraPaths": extraPaths,
        }

        if self.params.use_arcadia_root:
            pyrightconfig["include"] = self.params.rel_targets
            pyrightconfig["exclude"] = self.gen_pyrights_excludes(srcdirs)
            _write_config(pyrightconfig, os.path.join(self.params.arc_root, 'pyrightconfig.json'))
        else:
            for target in self.params.rel_targets:
                _write_config(pyrightconfig, os.path.join(self.params.arc_root, target, 'pyrightconfig.json'))

    def gen_black_settings(self, srcdirs):
        try:
            black_binary_path = yalibrary.tools.tool('black')
        except Exception as e:
            ide_common.emit_message("[[warn]]Could not get 'ya tool black': %s[[rst]]" % repr(e))
            return {}

        success = vscode.workspace.setup_linter_config(
            self.params.arc_root,
            const.DefaultLinterConfig.Python,
            const.PythonLinterName.Black,
        )
        if not success:
            return {}

        return OrderedDict(
            (
                (
                    "[python]",
                    {"editor.defaultFormatter": "ms-python.black-formatter", "editor.formatOnSaveMode": "file"},
                ),
                ("black-formatter.path", [black_binary_path]),
            )
        )

    def do_codegen(self):
        ide_common.emit_message("Running codegen")
        build_params = copy.deepcopy(self.params)
        build_params.add_result = list(self.CODEGEN_EXTS)
        build_params.replace_result = True
        build_params.force_build_depends = True
        build_params.continue_on_fail = True
        devtools.ya.app.execute(action=bh.do_ya_make, respawn=devtools.ya.app.RespawnType.NONE)(build_params)

    @property
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
        venv_opts.venv_tmp_project = self.venv_tmp_project
        venv_params = devtools.ya.core.yarg.merge_params(venv_opts.params(), copy.deepcopy(self.params))
        if os.path.exists(venv_opts.venv_tmp_project):
            ide_common.emit_message(f'Removing existing venv temporary project: {venv_opts.venv_tmp_project}')
            shutil.rmtree(venv_opts.venv_tmp_project)
        ide_common.emit_message(f'Generating venv: {venv_params.venv_root}')
        devtools.ya.app.execute(venv.gen_venv, respawn=devtools.ya.app.RespawnType.NONE)(venv_params)
        return os.path.join(venv_params.venv_root, 'bin', 'python')

    def get_srcdirs(self, modules):
        py_srcdirs = {}
        for module in modules.values():
            if module.get('MODULE_LANG') == 'PY3':
                srcdirs = module.get('SrcDirs', [])
                if isinstance(srcdirs, list):
                    py_srcdirs[module['module_path']] = srcdirs
                else:
                    py_srcdirs[module['module_path']] = [srcdirs]
        return py_srcdirs

    def mine_python_path(self, links_dir, srcdirs):
        source_paths = set()

        def find_py_namespace(node):
            for child in node.children:
                if isinstance(child, yalibrary.makelists.macro_definitions.PyNamespaceValue) and child.value:
                    return child.value
                if (
                    isinstance(child, yalibrary.makelists.macro_definitions.Macro)
                    and child.name == 'PY_NAMESPACE'
                    and len(child.children) > 0
                    and isinstance(child.children[0], yalibrary.makelists.macro_definitions.Value)
                    and child.children[0].key()
                ):
                    return child.children[0].key()
                namespace = find_py_namespace(child)
                if namespace:
                    return namespace

        def is_top_level(node):
            if isinstance(node, yalibrary.makelists.macro_definitions.SrcValue) and node.name == 'TOP_LEVEL':
                return True
            return any(is_top_level(child) for child in node.children)

        def is_protobuf(node):
            if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == 'PROTO_LIBRARY':
                return True
            return any(is_flatbuf(child) for child in node.children)

        def is_flatbuf(node):
            if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == 'FBS_LIBRARY':
                return True
            if isinstance(node, yalibrary.makelists.macro_definitions.SrcValue) and node.name.endswith('.fbs'):
                return True
            return any(is_flatbuf(child) for child in node.children)

        def has_srcs(node):
            if isinstance(node, yalibrary.makelists.macro_definitions.Srcs):
                return True
            return any(has_srcs(child) for child in node.children)

        def root_src_path(path, namespace):
            path_hash = "{}_{}".format(namespace, hashlib.md5((path + namespace).encode('utf-8')).hexdigest()[:8])
            name_parts = namespace.split('.') if (namespace and namespace != '.') else []
            while name_parts and path:
                name_part = name_parts.pop()
                path, path_part = os.path.split(path)
                if name_part != path_part:
                    module_virtual_dir = os.path.join(links_dir, path_hash)
                    base_virtual_dir = os.path.join(module_virtual_dir, *name_parts)
                    fs.ensure_dir(base_virtual_dir)
                    link_path = os.path.join(base_virtual_dir, name_part)
                    if os.path.exists(link_path):
                        os.unlink(link_path)
                    os.symlink(os.path.join(self.params.arc_root, path, path_part), link_path)
                    return module_virtual_dir
            if path:
                return os.path.join(self.params.arc_root, path)
            return self.params.arc_root

        for module_dir, module_srcdirs in srcdirs.items():
            try:
                makelist = yalibrary.makelists.ArcProject(self.params.arc_root, module_dir).makelist()
            except Exception as e:
                ide_common.emit_message(f'[[warn]]Error in module "{module_dir}": {repr(e)}[[rst]]')
                continue
            if not makelist:
                continue

            namespace = find_py_namespace(makelist)
            if namespace is None:
                if is_top_level(makelist) or is_flatbuf(makelist):
                    namespace = "."
                elif is_protobuf(makelist):
                    namespace = module_dir.replace('/', '.')
                elif has_srcs(makelist):
                    namespace = "."
                else:
                    namespace = module_dir.replace('/', '.')
            for src_dir in module_srcdirs:
                source_paths.add(root_src_path(src_dir, namespace))

        source_paths.discard(self.params.arc_root)
        return sorted(source_paths) + [self.params.arc_root]

    def mine_py_main(self, modules):
        def get_py_main_value(node):
            for child in node.children:
                if isinstance(child, yalibrary.makelists.macro_definitions.Value):
                    return child.name

        def get_main_value(node):
            main_is_next = False
            for child in node.children:
                if isinstance(child, yalibrary.makelists.macro_definitions.SrcValue):
                    if main_is_next:
                        return child.name
                    if child.name == 'MAIN':
                        main_is_next = True

        def find_main(node):
            if isinstance(node, yalibrary.makelists.macro_definitions.Macro) and node.name == 'PY_MAIN':
                main = get_py_main_value(node)
                if main:
                    return self.PyMain(main)

            if isinstance(node, yalibrary.makelists.macro_definitions.PySrcs):
                main = get_main_value(node)
                if main:
                    return main

            for child in node.children:
                main = find_main(child)
                if main:
                    return main

        for _, module in modules.items():
            if module.get('MANGLED_MODULE_TYPE') != 'PY3_BIN__from__PY3_PROGRAM':
                continue
            try:
                makelist = yalibrary.makelists.ArcProject(self.params.arc_root, module['module_path']).makelist()
            except Exception as e:
                ide_common.emit_message(
                    '[[warn]]Error in module "{}": {}[[rst]]'.format(module['module_path'], repr(e))
                )
                continue

            if not makelist:
                continue

            module['py_main'] = find_main(makelist) or '__main__.py'

    def gen_run_configurations(self, modules):
        COMMON_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in self.common_args)
        tasks, configurations = [], []
        run_modules = [m for m in modules.items() if m[1].get('MANGLED_MODULE_TYPE') == 'PY3_BIN__from__PY3_PROGRAM']
        for index, (name, module) in enumerate(sorted(run_modules, key=lambda item: item[0])):
            name = name.replace('/', '\uff0f')
            tasks.append(
                OrderedDict(
                    (
                        ("label", "Build: %s" % name),
                        ("type", "shell"),
                        (
                            "command",
                            "%s make --build=release %s %s"
                            % (
                                self.YA_PATH,
                                COMMON_ARGS,
                                exts.shlex2.quote(os.path.join(self.params.arc_root, module['module_path'])),
                            ),
                        ),
                        ("group", "build"),
                    )
                )
            )

            conf = OrderedDict(
                (
                    ("name", name),
                    ("type", "debugpy"),
                    ("request", "launch"),
                    ("args", []),
                    ("env", {"PYDEVD_USE_CYTHON": "NO"}),  # FIXME Workaround for pydevd not supporting Python 3.11
                    (
                        "presentation",
                        {
                            "group": "Run",
                            "order": index,
                        },
                    ),
                )
            )

            py_main = module.get("py_main")
            if isinstance(py_main, self.PyMain):
                conf["module"] = py_main.split(':')[0]
            elif py_main:
                conf["program"] = os.path.join(self.params.arc_root, module['module_path'], py_main)
            else:
                continue

            configurations.append(conf)

        return tasks, configurations

    def gen_test_configurations(self, modules, test_wrappers_dir):
        COMMON_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in self.common_args)
        tasks, configurations = [], []
        test_modules = [
            m
            for m in modules.items()
            if m[1].get('MANGLED_MODULE_TYPE') in ('PY3TEST_PROGRAM__from__PY23_TEST', 'PY3TEST_PROGRAM__from__PY3TEST')
        ]
        for index, (name, module) in enumerate(sorted(test_modules, key=lambda item: item[0]), 1):
            name = (os.path.dirname(name) or name).replace('/', '\uff0f')
            python_wrapper_dir = os.path.join(test_wrappers_dir, os.path.basename(module['path']))
            fs.ensure_dir(python_wrapper_dir)
            python_wrapper_path = os.path.join(python_wrapper_dir, 'python')
            python_wrapper_content = TEST_WRAPPER_TEMPLATE.format(
                arc_root=self.params.arc_root,
                path=os.path.join(self.params.arc_root, module['path']),
                test_context=os.path.join(
                    self.params.arc_root, module['module_path'], 'test-results', 'py3test', 'test.context'
                ),
            )
            with open(python_wrapper_path, 'w') as f:
                f.write(python_wrapper_content)
            fs.set_execute_bits(python_wrapper_path)

            prepare_task_name = "Prepare test: %s" % name
            tasks.append(
                OrderedDict(
                    (
                        ("label", prepare_task_name),
                        ("type", "shell"),
                        (
                            "command",
                            "%s make --run-all-tests --build=release --regular-tests --keep-going --test-prepare --keep-temps %s %s"
                            % (
                                self.YA_PATH,
                                COMMON_ARGS,
                                exts.shlex2.quote(os.path.join(self.params.arc_root, module['module_path'])),
                            ),
                        ),
                        ("group", "build"),
                    )
                )
            )
            tasks.append(
                OrderedDict(
                    (
                        ("label", "Test: %s" % name),
                        ("type", "shell"),
                        (
                            "command",
                            "%s make --run-all-tests --build=release %s %s"
                            % (
                                self.YA_PATH,
                                COMMON_ARGS,
                                exts.shlex2.quote(os.path.join(self.params.arc_root, module['module_path'])),
                            ),
                        ),
                        ("group", "test"),
                    )
                )
            )

            configurations.append(
                OrderedDict(
                    (
                        ("name", name),
                        ("type", "python"),
                        ("request", "launch"),
                        ("args", []),
                        ("env", {"PYDEVD_USE_CYTHON": "NO"}),  # FIXME Workaround for pydevd not supporting Python 3.11
                        ("cwd", self.params.arc_root),
                        ("module", "library.python.pytest.main"),
                        ("justMyCode", True),
                        ("python", python_wrapper_path),
                        (
                            "presentation",
                            {
                                "group": "Tests",
                                "order": index,
                            },
                        ),
                        ("preLaunchTask", prepare_task_name),
                    )
                )
            )

        return tasks, configurations

    def get_workspace_template(self):
        TARGETS = ' '.join(exts.shlex2.quote(arg) for arg in self.params.abs_targets)
        COMMON_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in self.common_args)
        codegen_args = self.common_args + ['--add-result=%s' % ext for ext in self.CODEGEN_EXTS]
        CODEGEN_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in codegen_args)
        venv_args = self.params.ya_make_extra + [
            '--venv-root=%s' % os.path.join(self.project_root, 'venv'),
            '--venv-add-tests',
            '--venv-tmp-project=%s' % self.venv_tmp_project,
        ]
        VENV_ARGS = ' '.join(exts.shlex2.quote(arg) for arg in venv_args)

        template = OrderedDict(
            (
                ("folders", []),
                (
                    "extensions",
                    OrderedDict(
                        (
                            (
                                "recommendations",
                                [
                                    "ms-python.python",
                                    "ms-python.vscode-pylance",
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
                            ("go.useLanguageServer", False),
                            ("search.followSymlinks", False),
                            ("git.mergeEditor", False),
                            ("npm.autoDetect", "off"),
                            ("task.autoDetect", "off"),
                            ("typescript.tsc.autoDetect", "off"),
                            ("python.languageServer", "Pylance"),
                            ("python.analysis.autoSearchPaths", False),
                            ("python.analysis.diagnosticMode", "openFilesOnly"),
                            ("python.analysis.enablePytestSupport", False),
                            ("python.analysis.indexing", self.params.python_index_enabled),
                            ("python.analysis.persistAllIndices", True),
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
                                            (
                                                "command",
                                                self.CODEGEN_TASK.format(
                                                    args=CODEGEN_ARGS, targets=TARGETS, ya_path=self.YA_PATH
                                                ),
                                            ),
                                            ("group", "build"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "<Regenerate workspace>"),
                                            ("type", "shell"),
                                            (
                                                "command",
                                                self.YA_PATH
                                                + " "
                                                + ' '.join(exts.shlex2.quote(arg) for arg in sys.argv[1:]),
                                            ),
                                            ("options", OrderedDict((("cwd", os.path.abspath(os.curdir)),))),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "<Rebuild venv>"),
                                            ("type", "shell"),
                                            ("command", f"{self.YA_PATH} ide venv {VENV_ARGS} {TARGETS}"),
                                            ("group", "build"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "Build: ALL (release)"),
                                            ("type", "shell"),
                                            ("command", f"{self.YA_PATH} make {COMMON_ARGS} -r {TARGETS}"),
                                            ("group", "build"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "Test: ALL (small)"),
                                            ("type", "shell"),
                                            (
                                                "command",
                                                f"{self.YA_PATH} make -t -r {COMMON_ARGS} {TARGETS}",
                                            ),
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
                                                "%s make -t -r --test-size=MEDIUM %s %s"
                                                % (self.YA_PATH, COMMON_ARGS, TARGETS),
                                            ),
                                            ("group", "test"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "Test: ALL (small + medium)"),
                                            ("type", "shell"),
                                            (
                                                "command",
                                                f"{self.YA_PATH} make -tt -r {COMMON_ARGS} {TARGETS}",
                                            ),
                                            ("group", "test"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "Test: ALL (large)"),
                                            ("type", "shell"),
                                            (
                                                "command",
                                                "%s make -t --test-size=LARGE -r %s %s"
                                                % (self.YA_PATH, COMMON_ARGS, TARGETS),
                                            ),
                                            ("group", "test"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "Test: ALL (small + medium + large)"),
                                            ("type", "shell"),
                                            (
                                                "command",
                                                f"{self.YA_PATH} make -tA -r {COMMON_ARGS} {TARGETS}",
                                            ),
                                            ("group", "test"),
                                        )
                                    ),
                                    OrderedDict(
                                        (
                                            ("label", "Test: ALL (restart failed)"),
                                            ("type", "shell"),
                                            (
                                                "command",
                                                f"{self.YA_PATH} make -tA -X -r {COMMON_ARGS} {TARGETS}",
                                            ),
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
                            ("configurations", []),
                        )
                    ),
                ),
            )
        )

        if self.params.black_enabled:
            template["extensions"]["recommendations"].append("ms-python.black-formatter")

        return template


def gen_vscode_workspace(params):
    ide_common.emit_message(
        "[[warn]]DEPRECATED: 'ya ide vscode-py' [[rst]]is not supported anymore. Use [[good]]'ya ide vscode --py3'[[rst]] instead.\n"
        "[[c:dark-cyan]]https://docs.yandex-team.ru/ya-make/usage/ya_ide/vscode#ya-ide-vscode[[rst]]"
    )
    if pm.my_platform() == 'win32':
        ide_common.emit_message("[[bad]]Handler 'vscode-py' doesn't work on Windows[[rst]]")
        return

    orig_flags = copy.copy(params.flags)
    ya_make_opts = devtools.ya.core.yarg.merge_opts(build_opts.ya_make_options(free_build_targets=True))
    params.ya_make_extra.extend(['-DBUILD_LANGUAGES=PY3'])
    extra_params = ya_make_opts.initialize(params.ya_make_extra)
    params = devtools.ya.core.yarg.merge_params(extra_params, params)
    params.flags.update(extra_params.flags)
    project = PyProject(params, orig_flags)
    project.gen_workspace()
