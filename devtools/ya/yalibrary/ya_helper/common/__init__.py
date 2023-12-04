# -*- coding: utf-8 -*-

"""
Common utilities
"""

from __future__ import division, print_function, unicode_literals

import os
import six
import typing as tp

from .run_subprocess import run_subprocess, check_subprocess, SubprocessError, CalledProcessError  # noqa: F401
from .logger_count import LoggerCounter  # noqa: F401
from .gsid import GSIDParts  # noqa: F401
from .fs_utils import make_folder  # noqa: F401


def check_unpacked_args(unpacked):
    if not unpacked:
        return

    for item in unpacked:
        if isinstance(item, (list, tuple, set)):
            raise TypeError("Maybe you forget * before parameter?")


class CleanEnv(LoggerCounter):
    def __init__(self, env=None):
        # type: (dict) -> None
        self.env = env or {}
        self.old_env = {}

    def __enter__(self):
        self.logger.info("Enter with context with isolated environment")

        self.old_env = os.environ.copy()
        os.environ.clear()
        for k, v in six.itervalues(self.env):
            os.environ[k] = v

    def __exit__(self, *args):
        self.logger.info("Exit from context with isolated environment")
        os.environ.clear()
        os.environ.update(self.old_env)
        return False


def level_up(s, indent=4, first_symbol='|'):
    if not isinstance(s, (tuple, list, set)):
        s = (s,)
    s = '\n'.join(map(str, s))

    if indent > 0:
        _indent = " " * (indent - 2) + first_symbol + " "
    else:
        _indent = ''

    lines = (
        # Be careful: this is generator
        "{}{}".format(_indent, line)
        for line in s.split("\n")
    )

    return "\n".join(lines)


_K = tp.TypeVar("_K")


def sorted_items(d):
    # type: (dict[tp.Any, _K]) -> tp.Iterable[tuple[tp.Any, _K]]
    """For python2 compatibility"""
    for key in sorted(d.keys(), key=lambda k: (k is None, isinstance(k, (float, int)), k)):
        yield key, d[key]
