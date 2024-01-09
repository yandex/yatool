from __future__ import absolute_import
import json

import six
import yaml
import logging
import subprocess

from exts import func

import core.resource
import yalibrary.tools
from .state_helper import stop


logger = logging.getLogger(__name__)


@func.lazy
def config():
    style_config = core.resource.try_get_resource('config.clang-format')

    return json.dumps(yaml.safe_load(style_config))


def fix(data, args):
    popen_python_kwargs = {}
    if six.PY3:
        popen_python_kwargs["text"] = True
    p = subprocess.Popen(
        [yalibrary.tools.tool('clang-format'), '-assume-filename=a.cpp', '-style=' + config()],
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
        raise Exception('error while running clang-format: ' + err)

    return out


def fix_header(data):
    expected = "#pragma once"
    for line in data.split('\n'):
        if line.strip() == "" or line.strip().startswith("//"):
            continue
        elif line.strip() == expected:
            return data
        return expected + '\n\n' + data

    return data


def fix_clang_format(path, data, args):
    if path.endswith('.h'):
        data = fix_header(data)

    return fix(data, args)
