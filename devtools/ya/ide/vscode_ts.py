from collections import OrderedDict

import os
import sys
import json
import platform
import subprocess

import termcolor

import devtools.ya.core.yarg
import exts.shlex2
import yalibrary.tools

from devtools.ya.ide import ide_common, vscode


class VSCodeTypeScriptOptions(devtools.ya.core.yarg.Options):
    GROUP = devtools.ya.core.yarg.Group('VSCode workspace options', 0)

    def __init__(self):
        self.project_output = None
        self.workspace_name = None
        self.build_enabled = False
        self.install_deps = False

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
                ['--install-deps'],
                help='Install project dependencies',
                hook=devtools.ya.core.yarg.SetConstValueHook('install_deps', True),
                group=cls.GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--build-target'],
                help='Run build additionally',
                hook=devtools.ya.core.yarg.SetConstValueHook('build_enabled', True),
                group=cls.GROUP,
            ),
        ]

    def postprocess(self):
        pass


FINISH_HELP = (
    'Workspace file '
    + termcolor.colored('%s', 'green', attrs=['bold'])
    + ' is ready\n'
    + 'Code navigation and autocomplete configured built-in '
    + termcolor.colored('TypeScript', 'green')
    + ' plugin'
)


def run_nots(params, *args):
    nots = yalibrary.tools.tool('nots')
    target_dir = params.abs_targets[0]
    process = subprocess.Popen([nots] + list(args), cwd=target_dir, stdout=sys.stdout, stderr=sys.stderr)
    return process.wait()


def do_install_deps(params):
    ide_common.emit_message('Installing dependencies')
    if run_nots(params, 'install') != 0:
        ide_common.emit_message(termcolor.colored('Build failed. Terminating', 'red'))
        sys.exit(1)


def do_build(params):
    ide_common.emit_message('Running build')
    if run_nots(params, 'build') != 0:
        ide_common.emit_message(termcolor.colored('Build failed. Terminating', 'red'))
        sys.exit(1)


def get_workspace_template(params, YA_PATH):
    target_dir = params.abs_targets[0]
    template = OrderedDict(
        (
            ('folders', []),
            (
                'extensions',
                OrderedDict(
                    (
                        (
                            'recommendations',
                            [
                                'EditorConfig.EditorConfig',
                                'Orta.vscode-jest',
                                'dbaeumer.vscode-eslint',
                                'esbenp.prettier-vscode',
                                'forbeslindesay.forbeslindesay-taskrunner',
                            ],
                        ),
                        ("unwantedRecommendations", ["ms-vscode.cmake-tools"]),
                    )
                ),
            ),
            (
                'settings',
                OrderedDict(
                    (
                        ("C_Cpp.intelliSenseEngine", "disabled"),
                        ("go.useLanguageServer", False),
                        ('editor.defaultFormatter', 'esbenp.prettier-vscode'),
                        ("forbeslindesay-taskrunner.separator", ": "),
                        ('git.mergeEditor', False),
                        ('jest.runMode', "on-demand"),
                        ('npm.autoDetect', 'off'),
                        ('npm.packageManager', 'pnpm'),
                        ('remote.autoForwardPorts', True),
                        ('search.followSymlinks', False),
                        (
                            'search.exclude',
                            OrderedDict(
                                (
                                    ('**/*.code-search', True),
                                    ('**/.cache', True),
                                    ('**/.figmaImages', True),
                                    ('**/.git', True),
                                    ('**/.github', True),
                                    ('**/.graphql-schemas', True),
                                    ('**/.nuxt', True),
                                    ('**/.output', True),
                                    ('**/.pnpm', True),
                                    ('**/.tanker', True),
                                    ('**/.tsbuild', True),
                                    ('**/.vscode', True),
                                    ('**/.yarn', True),
                                    ('**/__bundle-size-checker', True),
                                    ('**/__reports', True),
                                    ('**/bower_components', True),
                                    ('**/build', True),
                                    ('**/coverage', True),
                                    ('**/dist/**', True),
                                    ('**/docs-build', True),
                                    ('**/graphql-schemas', True),
                                    ('**/logs', True),
                                    ('**/node_modules', True),
                                    ('**/out/**', True),
                                    ('**/package-lock.json', True),
                                    ('**/pnpm-lock.yaml', True),
                                    ('**/secrets', True),
                                    ('**/storybook-static', True),
                                    ('**/tmp', True),
                                    ('**/yarn.lock', True),
                                )
                            ),
                        ),
                        (
                            'remote.SSH.defaultForwardedPorts',
                            [
                                OrderedDict((('name', 'Hermione GUI'), ('localPort', 8000), ('remotePort', 8000))),
                                OrderedDict((('name', 'Storybook'), ('localPort', 9010), ('remotePort', 9010))),
                            ],
                        ),
                        ('task.autoDetect', 'off'),
                        ('typescript.preferences.importModuleSpecifier', 'relative'),
                        ('typescript.tsc.autoDetect', 'off'),
                        (
                            'typescript.tsdk',
                            'node_modules/typescript/lib',
                        ),  # override default typescript version built-in to VSCode
                        (
                            'typescript.updateImportsOnFileMove.enabled',
                            'never',
                        ),  # TS Language Server don't support aliases
                        ("yandex.arcRoot", params.arc_root),
                        (
                            "yandex.codenv",
                            {
                                "languages": ["TS"],
                                "targets": params.rel_targets,
                                "arguments": [],
                                "autolaunch": False,
                            },
                        ),
                    )
                ),
            ),
            (
                'tasks',
                OrderedDict(
                    (
                        ('version', '2.0.0'),
                        (
                            'tasks',
                            [
                                OrderedDict(
                                    (
                                        ('label', 'nots: Build'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots build'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'build'),
                                                    ('isDefault', True),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', 'nots: Install dependencies'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots install'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'build'),
                                                    ('isDefault', False),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', 'nots: Update lockfile'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots update-lockfile'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'build'),
                                                    ('isDefault', False),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', 'nots: Test'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots test'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'test'),
                                                    ('isDefault', True),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', 'nots: Lint'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots lint'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'test'),
                                                    ('isDefault', False),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', 'nots: Code reformat'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots fmt'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'test'),
                                                    ('isDefault', False),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', 'nots: Check dependencies'),
                                        ('type', 'shell'),
                                        ('command', YA_PATH + ' tool nots check-deps'),
                                        ('options', OrderedDict((('cwd', target_dir),))),
                                        (
                                            'group',
                                            OrderedDict(
                                                (
                                                    ('kind', 'test'),
                                                    ('isDefault', False),
                                                )
                                            ),
                                        ),
                                    )
                                ),
                                OrderedDict(
                                    (
                                        ('label', '<Regenerate workspace>'),
                                        ('type', 'shell'),
                                        (
                                            'command',
                                            YA_PATH + ' ' + ' '.join(exts.shlex2.quote(arg) for arg in sys.argv[1:]),
                                        ),
                                        ('options', OrderedDict((('cwd', os.path.abspath(os.curdir)),))),
                                    )
                                ),
                            ],
                        ),
                    )
                ),
            ),
            (
                'launch',
                OrderedDict(
                    (
                        ('version', '0.2.0'),
                        (
                            'configurations',
                            [
                                OrderedDict(
                                    (
                                        ('type', 'node'),
                                        ('runtimeExecutable', 'pnpm'),
                                        ('name', 'vscode-jest-tests.v2'),
                                        ('request', 'launch'),
                                        ('program', '${workspaceFolder}/node_modules/.bin/jest'),
                                        (
                                            'args',
                                            [
                                                '--runInBand',
                                                '--watchAll=false',
                                                '--testNamePattern',
                                                '${jest.testNamePattern}',
                                                '--runTestsByPath',
                                                '${jest.testFile}',
                                            ],
                                        ),
                                        ('cwd', '${workspaceFolder}'),
                                        ('console', 'integratedTerminal'),
                                        ('internalConsoleOptions', 'neverOpen'),
                                    )
                                )
                            ],
                        ),
                    )
                ),
            ),
        )
    )

    return template


def gen_vscode_workspace(params):
    if len(params.abs_targets) == 0:
        ide_common.emit_message('[[bad]]Project target not found[[rst]]')
        return
    if len(params.abs_targets) > 1:
        ide_common.emit_message('[[bad]]Multiple targets are not supported at this moment[[rst]]')
        return
    if params.project_output:
        project_root = os.path.abspath(os.path.expanduser(params.project_output))
        if not os.path.exists(project_root):
            ide_common.emit_message(f'Creating directory: {project_root}')
            os.makedirs(project_root)
    else:
        project_root = os.path.abspath(os.curdir)

    if params.install_deps:
        do_install_deps(params)

    if params.build_enabled:
        do_build(params)

    YA_PATH = os.path.join(params.arc_root, 'ya')

    workspace = get_workspace_template(params, YA_PATH)

    workspace['folders'] = [
        OrderedDict(
            (
                ('name', target),
                ('path', os.path.join(params.arc_root, target)),
            )
        )
        for target in params.rel_targets
    ]

    workspace_path = vscode.workspace.pick_workspace_path(project_root, params.workspace_name)
    if os.path.exists(workspace_path):
        vscode.workspace.merge_workspace(workspace, workspace_path)

    ide_common.emit_message(f'Writing {workspace_path}')
    with open(workspace_path, 'w') as f:
        json.dump(workspace, f, indent=4, ensure_ascii=True)

    ide_common.emit_message(FINISH_HELP % workspace_path)

    if os.getenv('SSH_CONNECTION'):
        remote_ide_link = 'vscode://vscode-remote/ssh-remote+{hostname}{workspace_path}?windowId=_blank'
        ide_common.emit_message(remote_ide_link.format(hostname=platform.node(), workspace_path=workspace_path))
    else:
        local_ide_link = 'vscode://file/{workspace_path}?windowId=_blank'
        ide_common.emit_message(local_ide_link.format(workspace_path=workspace_path))
