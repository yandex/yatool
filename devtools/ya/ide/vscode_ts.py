from __future__ import absolute_import
from collections import OrderedDict

import os
import sys
import json
import platform
import subprocess

import termcolor

import core.common_opts
import core.yarg
import exts.shlex2
import yalibrary.tools

from ide import ide_common, vscode


class VSCodeTypeScriptOptions(core.yarg.Options):
    GROUP = core.yarg.Group('VSCode workspace options', 0)

    def __init__(self):
        self.project_output = None
        self.workspace_name = None
        self.build_enabled = False

    @classmethod
    def consumer(cls):
        return [
            core.yarg.ArgConsumer(
                ['-P', '--project-output'],
                help='Custom IDE workspace output directory',
                hook=core.yarg.SetValueHook('project_output'),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ['-W', '--workspace-name'],
                help='Custom IDE workspace name',
                hook=core.yarg.SetValueHook('workspace_name'),
                group=cls.GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--build-target'],
                help='Run build additionally',
                hook=core.yarg.SetConstValueHook('build_enabled', True),
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


def do_build(params):
    ide_common.emit_message('Running build')

    nots = yalibrary.tools.tool('nots')
    process = subprocess.Popen([nots, 'build'])
    returncode = process.wait()

    if returncode != 0:
        ide_common.emit_message(termcolor.colored('Build failed. Terminating', 'red'))
        sys.exit(1)


def get_workspace_template(params, YA_PATH):
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
                    )
                ),
            ),
            (
                'settings',
                OrderedDict(
                    (
                        ('editor.defaultFormatter', 'esbenp.prettier-vscode'),
                        ('eslint.packageManager', 'pnpm'),
                        ('git.mergeEditor', False),
                        ('jest.autoRun', OrderedDict((('watch', False),))),
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
                    )
                ),
            ),
            (
                'tasks',
                OrderedDict(
                    (
                        ('versions', '2.0.0'),
                        (
                            'tasks',
                            [
                                OrderedDict(
                                    (
                                        ('label', 'Build'),
                                        ('type', 'shell'),
                                        ('command', 'ya tool nots build'),
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
                                        ('label', 'Update lockfile'),
                                        ('type', 'shell'),
                                        ('command', 'ya tool nots update-lockfile'),
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
                                        ('label', 'Test'),
                                        ('type', 'shell'),
                                        ('command', 'ya tool nots test'),
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
                                        ('label', 'Code reformat'),
                                        ('type', 'shell'),
                                        ('command', 'ya tool nots fmt'),
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
                                        ('label', 'Check deps'),
                                        ('type', 'shell'),
                                        ('command', 'ya tool nots check-deps'),
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
    if params.project_output:
        project_root = os.path.abspath(os.path.expanduser(params.project_output))
        if not os.path.exists(project_root):
            ide_common.emit_message('Creating directory: {}'.format(project_root))
            os.makedirs(project_root)
    else:
        project_root = os.path.abspath(os.curdir)

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

    ide_common.emit_message('Writing {}'.format(workspace_path))
    with open(workspace_path, 'w') as f:
        json.dump(workspace, f, indent=4, ensure_ascii=True)

    ide_common.emit_message(FINISH_HELP % workspace_path)

    if os.getenv('SSH_CONNECTION'):
        remote_ide_link = 'vscode://vscode-remote/ssh-remote+{hostname}{workspace_path}?windowId=_blank'
        ide_common.emit_message(remote_ide_link.format(hostname=platform.node(), workspace_path=workspace_path))
