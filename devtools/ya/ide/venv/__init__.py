import logging
import os
import subprocess
import six

import devtools.ya.build.build_opts
import devtools.ya.core.yarg
import devtools.ya.core.config
import exts.fs
import yalibrary.platform_matcher as pm
import exts.path2

from . import project


PYTHON_BINARY = 'yapython'
BUILD_OUTPUT = 'ya_build_output'
SOURCE_ROOT_KEY = 'arcadia-source-root'
CONF_BASENAME = 'pyvenv.cfg'

logger = logging.getLogger(__name__)


class CreateVenvError(Exception):
    mute = True


def do_venv(params):
    if pm.my_platform() == 'win32':
        logger.error("Handler 'venv' doesn't work on Windows")
        return
    ya_make_opts = devtools.ya.core.yarg.merge_opts(
        devtools.ya.build.build_opts.ya_make_options(free_build_targets=True)
    )
    params.ya_make_extra.append('-DBUILD_LANGUAGES=PY3')
    params = devtools.ya.core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra), params)
    _check_paths(params)
    gen_venv(params)


def _check_paths(params):
    for rel_target in params.rel_targets:
        path = os.path.join(params.arc_root, rel_target)
        if not os.path.exists(path):
            raise CreateVenvError(f"Target '{rel_target}' doesn't exist")


def gen_venv(params):
    import app_ctx

    exe_path = _build_python(app_ctx, params)
    _create_venv(params, exe_path)
    _update_config(params)
    if params.venv_with_pip:
        _install_pip(params)


def _build_python(app_ctx, params):
    build_output = os.path.join(params.venv_root, BUILD_OUTPUT)
    exts.fs.create_dirs(build_output)
    with project.Project(params, app_ctx, output_path=build_output, exe_name=PYTHON_BINARY) as pyproj:
        logger.debug('Project: %s', pyproj.project)
        logger.debug('Source: %s', pyproj.source_path)
        logger.debug('Output: %s', pyproj.output_path)
        logger.debug('Exe: %s', pyproj.exe_path)
        pyproj.prepare()
        pyproj.build()
        return pyproj.exe_path


def _run_cmd(cmd, env=None):
    logger.debug('Execute: %s', cmd)
    with subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, env=env) as python:
        _, stderr = python.communicate()
        if python.returncode != 0:
            raise CreateVenvError(
                "Command '{}' failed with exit code={} and output:\n{}".format(
                    ' '.join(cmd), python.returncode, six.ensure_text(stderr)
                )
            )


def _create_venv(params, exe_path):
    exts.fs.ensure_removed(os.path.join(params.venv_root, 'bin'))  # venv doesn't replace existing symlinks
    build_venv_cmd = [exe_path, '-mvenv', '--symlinks', '--without-pip', params.venv_root]
    env = {
        'Y_PYTHON_SOURCE_ROOT': params.arc_root,
    }
    _run_cmd(build_venv_cmd, env)


def _update_config(params):
    with open(os.path.join(params.venv_root, CONF_BASENAME), 'a') as f:
        f.write(f'{SOURCE_ROOT_KEY} = {params.arc_root}\n')


def _install_pip(params):
    install_pip_cmd = [
        os.path.join(params.venv_root, 'bin', 'python'),
        '-mpip',
        'install',
        '--index-url=https://pypi.yandex-team.ru/simple/',
        '--force-reinstall',
        '--no-deps',
        'pip',
    ]
    _run_cmd(install_pip_cmd)


class VenvOptions(devtools.ya.core.yarg.Options):
    OPT_GROUP = devtools.ya.core.yarg.Group('Python venv options', -1)

    def __init__(self):
        self.venv_root = None
        self.venv_with_pip = False
        self.venv_add_tests = False
        self.venv_tmp_project = os.path.join('junk', devtools.ya.core.config.get_user(), '_ya_venv')
        self.venv_excluded_peerdirs = []

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--venv-root'],
                help='Virtual environment root (required)',
                hook=devtools.ya.core.yarg.SetValueHook('venv_root'),
                group=VenvOptions.OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--venv-with-pip'],
                help='Install pip to venv',
                hook=devtools.ya.core.yarg.SetConstValueHook('venv_with_pip', True),
                group=VenvOptions.OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--venv-add-tests'],
                help='Include tests',
                hook=devtools.ya.core.yarg.SetConstValueHook('venv_add_tests', True),
                group=VenvOptions.OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--venv-tmp-project'],
                help='Temporary project path',
                hook=devtools.ya.core.yarg.SetValueHook('venv_tmp_project'),
                group=VenvOptions.OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--venv-excluded-peerdirs'],
                help='Totally exclude specified peerdirs',
                hook=devtools.ya.core.yarg.SetAppendHook('venv_excluded_peerdirs'),
                group=VenvOptions.OPT_GROUP,
            ),
        ]

    def postprocess(self):
        if self.venv_root is None:
            raise devtools.ya.core.yarg.ArgsValidatingException(
                'Virtual environment root (--venv-root) must be specified.'
            )
        self.venv_root = exts.path2.abspath(self.venv_root, expand_user=True)
