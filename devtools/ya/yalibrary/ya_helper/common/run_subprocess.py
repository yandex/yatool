# -*- coding: utf-8 -*-

from __future__ import division, print_function, unicode_literals

from time import time

import logging

import devtools.ya.core.sec as sec
import os
import typing as tp  # noqa: F401
import six


if six.PY2:
    import subprocess32 as subprocess
else:
    import subprocess

SubprocessError = subprocess.SubprocessError
CalledProcessError = subprocess.CalledProcessError


class _SubprocessRunnerWrapper(object):
    counter = 0
    runner = None

    def __init__(self):
        self.runner = subprocess.run
        self.logger = logging.getLogger(__name__)

    def run_subprocess(self, cmd, env=None, original_env=False, **kwargs):
        # type: (tp.Iterable[str], tp.Optional[tp.Dict[str, str]], bool, tp.Any) -> subprocess.CompletedProcess
        """
        @raise: SubprocessError
        """
        type(self).counter += 1

        logger = self.logger.getChild(str(type(self).counter))

        env_ = {}
        if original_env:
            env_ = os.environ.copy()
        if env:
            env_.update(env)

        kwargs['env'] = env_
        kwargs['check'] = True
        kwargs.setdefault('stdout', subprocess.PIPE)
        kwargs.setdefault('stderr', subprocess.PIPE)

        cmd = tuple(map(str, cmd))
        logger.info("RUN %s", os.path.basename(cmd[0]))
        logger.debug("cmd: %s", cmd)
        logger.debug("env: %s", sec.environ(env_))
        cwd = kwargs.get('cwd', os.getcwd())
        logger.debug("cwd: %s", cwd)

        start = time()
        finish = time()
        returncode = "?"

        try:
            try:
                result = self.runner(cmd, **kwargs)
            finally:
                finish = time()

            result.check_returncode()
            returncode = result.returncode
        except (SubprocessError, CalledProcessError) as e:
            returncode = getattr(e, "returncode", "<unknown>")
            output = six.ensure_str(getattr(e, 'output', "<unknown>"))
            stderr = six.ensure_str(getattr(e, 'stderr', "<unknown>"))

            logger.error("==========================")
            logger.error("cwd: %s", cwd)
            logger.error("Failed command: %s", cmd)
            logger.error("With return code: %s", returncode)
            logger.exception("Exception:")
            logger.error("STDOUT: %s", output)
            logger.error("STDERR: %s", stderr)
            logger.error("==========================")
            raise
        finally:
            logger.info("Finish by %fs with exit code %s", finish - start, returncode)

        result.stdout = six.ensure_str(result.stdout)
        result.stderr = six.ensure_str(result.stderr)
        return result

    def check_subprocess(self, cmd, env=None, original_env=False, **kwargs):
        # type: (tp.Iterable[str], tp.Optional[tp.Dict[str, str]], bool, tp.Any) -> str
        """
        @raise: SubprocessError
        """
        result = self.run_subprocess(cmd=cmd, env=env, original_env=original_env, **kwargs)

        return result.stdout


_subprocess_runner_wrapper = _SubprocessRunnerWrapper()

run_subprocess = _subprocess_runner_wrapper.run_subprocess
check_subprocess = _subprocess_runner_wrapper.check_subprocess


def set_default_runner(runner):
    _subprocess_runner_wrapper.runner = runner
