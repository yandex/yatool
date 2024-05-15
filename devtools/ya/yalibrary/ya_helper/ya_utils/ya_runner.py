# -*- coding: utf-8 -*-

from __future__ import division, print_function, unicode_literals

from json import JSONEncoder

import json
import os
import six
import typing as tp  # noqa: F401

from .ya_options import YaBaseOptions

from yalibrary.ya_helper.common import LoggerCounter, make_folder, run_subprocess
from ..common.run_subprocess import CalledProcessError, SubprocessError


class _JSONEncoder(JSONEncoder):
    def default(self, o):
        return str(o)


class Ya(LoggerCounter):
    """Helper class for run Ya"""

    def __init__(
        self,
        options,  # type: YaBaseOptions
        name,  # type: str
        env=None,  # type: tp.Optional[dict]
        cwd=None,  # type: tp.Optional[str]
        create_new_pgrp=False,  # type: bool
    ):
        self.options = options
        self._original_env = env
        self.cwd = cwd
        self.process_group = 0 if create_new_pgrp else None

        self.name = name

        self.returncode = None
        self.stdout = None
        self.stderr = None

        if not isinstance(self.options, YaBaseOptions):
            raise TypeError("`options` parameter must be inheritor of YaBaseOptions")

        if not self.options.ya_bin:
            raise ValueError("Please, add `ya_bin` parameter to options")

        if not self.options.logs_dir:
            raise ValueError("Please, add `logs_dir` parameter to options")

        self._logs_dir = os.path.join(self.options.logs_dir, "ya_" + self.name)
        make_folder(self._logs_dir, exist_ok=False)

        self.error_file = self.options.error_file
        self.stderr_path = os.path.join(self._logs_dir, 'stderr.txt')

        self._dump_options()

        self.cmd, self.env = self.options.generate()

        if env:
            self.logger.warning(
                "You manually add some environment keys (%s). " "Please, hide it into YaBaseOptions instead", env.keys()
            )
            self.env.update(env)

    def run(self, reraise=True):
        """
        @raise: SubprocessError
        @return: str
        """
        if self.returncode is not None:
            self.logger.error("This instance has already been launched")
            raise RuntimeError("This instance has already been launched")

        try:
            result = run_subprocess(
                self.cmd, self.env, original_env=False, cwd=self.cwd, process_group=self.process_group
            )
            self.returncode = result.returncode
            self.stdout = result.stdout
            self.stderr = result.stderr
        except (SubprocessError, CalledProcessError) as e:
            self.returncode = getattr(e, "returncode", "<unknown>")
            self.stdout = six.ensure_str(getattr(e, 'output', ''))
            self.stderr = six.ensure_str(getattr(e, 'stderr', ''))

            if not self.stderr and self.error_file and os.path.exists(self.error_file):
                with open(self.error_file, "rt") as f:
                    self.stderr = "Stacktrace from ya:\n{}".format(f.read())

            if reraise:
                raise

            return False
        finally:
            if self.stdout:
                with open(os.path.join(self._logs_dir, 'stdout.txt'), 'wt') as f:
                    f.write(str(self.stdout))
            else:
                self.logger.warning("No stdout")
                self.stdout = "<unknown>"

            if self.stderr:
                with open(self.stderr_path, 'wt') as f:
                    f.write(str(self.stderr))
            else:
                self.logger.warning('No stderr')
                self.stderr = "<stderr>"

        return six.ensure_str(self.stdout)

    def check(self):
        if self.returncode is None:
            raise ValueError("Ya was not launched")
        if self.returncode != 0:
            raise ValueError("returncode must be 0")

    def __repr__(self):
        return "<{}:{}{}{}{}>".format(
            self.__class__.__name__,
            self.name,
            " {}".format(self.returncode) if self.returncode is not None else '',
            " ERR" if self.stderr is not None else '',
            " OUT" if self.stdout is not None else '',
        )

    def _dump_options(self):
        try:
            with open(os.path.join(self._logs_dir, "ya_options.dump.json"), "w") as f:
                json.dump(self.options.dict_without_secrets, f, cls=_JSONEncoder)
        except Exception:
            self.logger.exception("While dumping options")
            self.logger.debug("Options: %s", self.options.dict_without_secrets)
