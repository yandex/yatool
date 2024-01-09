from __future__ import absolute_import
import subprocess

import six

import core
import yalibrary
from .state_helper import stop


def python_config():
    return core.resource.try_get_resource('config.toml')


def fix_python_with_black(data, path, fast, args):
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
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, **popen_python_kwargs
    )
    out, err = p.communicate(input=data)

    # Abort styling on signal
    if p.returncode < 0:
        stop()

    if err:
        raise RuntimeError('error while running black on file "{}": {}'.format(path, err.strip()))

    return out
