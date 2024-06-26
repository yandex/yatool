from __future__ import absolute_import
import subprocess

import six

import build.plugins.lib.tests.utils as config_utils
import core
import exts
import yalibrary
from .ruff_config import load_ruff_config
from .state_helper import stop


RUFF_CONFIG_PATHS_FILE = 'build/config/tests/ruff/ruff_config_paths.json'


def python_config():
    # type() -> str
    return core.resource.try_get_resource('config.toml')


@exts.func.lazy
def get_config_paths():
    # type(str) -> dict
    return core.config.config_from_arc_rel_path(RUFF_CONFIG_PATHS_FILE)


def get_ruff_config(path):
    # type(str) -> RuffConfig
    arc_root = core.config.find_root(fail_on_error=False)
    if arc_root is None:
        raise RuntimeError('Can\'t find arcadia root, so can\'t find config for ruff')
    config_paths = get_config_paths()

    full_config_path = config_utils.select_project_config(path, config_paths, arc_root)
    return load_ruff_config(full_config_path)


def fix_python_with_black(data, path, fast, args):
    # type(str, str, bool, StyleOptions) -> str
    tool = 'black' if not args.py2 else 'black_py2'
    popen_python_kwargs = {}
    if six.PY3:
        popen_python_kwargs["text"] = True
    if fast:
        black_args = [yalibrary.tools.tool(tool), '--fast', '-q', '-', '--config', args.python_config_file.name]
    else:
        black_args = [yalibrary.tools.tool(tool), '-q', '-', '--config', args.python_config_file.name]

    p = subprocess.Popen(
        black_args,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        shell=False,
        **popen_python_kwargs
    )
    out, err = p.communicate(input=data)

    # Abort styling on signal
    if p.returncode < 0:
        stop()

    if err:
        raise RuntimeError('error while running black on file "{}": {}'.format(path, err.strip()))

    return out


def _launch_ruff(data, path, config_path, cmd_args):
    # type(str, str, str, list[str]) -> str

    ruff_args = [yalibrary.tools.tool('ruff')] + cmd_args + ['--config', config_path, '-']

    p = subprocess.Popen(
        ruff_args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, text=True
    )
    out, err = p.communicate(input=data)

    if p.returncode is None:
        with open('ruff.out', 'w') as fd:
            fd.write('Ruff out: {}'.format(out))
            fd.write('\nRuff err: {}'.format(err))
        error_msg = 'Something went wrong while running ruff {} on file "{}"'.format(' '.join(cmd_args), path)
        error_msg += '\nCheck file \'ruff.out\' for errors'
        raise RuntimeError(error_msg)

    # Abort styling on signal
    if p.returncode < 0:
        stop()

    if p.returncode != 0 and err:
        raise RuntimeError('error while running ruff {} on file "{}": {}'.format(' '.join(cmd_args), path, err.strip()))

    return out


def fix_python_with_ruff(data, path):
    # type(str, str) -> str
    if not six.PY3:
        error_msg = 'Ruff couldn\'t be called with python 2!'
        error_msg += '\nCall \'ya style\' like \'ya -3 style\''
        raise RuntimeError(error_msg)

    ruff_config = get_ruff_config(path)

    STDIN_FNAME = ['--stdin-filename', path]

    out = _launch_ruff(data, path, ruff_config.path, ['format'] + STDIN_FNAME)
    # launch check fix to sort imports
    out = _launch_ruff(out, path, ruff_config.path, ['check', '--fix'] + STDIN_FNAME)
    return out
