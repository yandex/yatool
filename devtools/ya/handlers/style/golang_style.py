from __future__ import absolute_import
import subprocess

import yalibrary
from .state_helper import stop

import six


def fix_golang_style(data, path):
    popen_python_kwargs = {}
    if six.PY3:
        popen_python_kwargs["text"] = True
    p = subprocess.Popen(
        [yalibrary.tools.tool('yoimports'), '-'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, **popen_python_kwargs
    )
    out, err = p.communicate(input=data)

    # Abort styling on signal
    if p.returncode < 0:
        stop()

    if err:
        raise Exception('error while running yoimports on file "{}": {}'.format(path, err.strip()))

    return out
