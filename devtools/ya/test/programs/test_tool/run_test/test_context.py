import json
import logging
import os
import sys

from devtools.ya.test import const

import six

logger = logging.getLogger(__name__)


class Context(object):
    def __init__(self, params, work_dir, ram_drive_path):
        self._ctx = {
            'build': {},
            'internal': {},
            'resources': {},
            'runtime': {},
        }
        self._filename = os.path.join(params.build_root, const.SUITE_CONTEXT_FILE_NAME)

        self._setup_context(params, work_dir, ram_drive_path)

        self._env = {}

    def _setup_context(self, params, work_dir, ram_drive_path):
        with open(params.context_filename) as afile:
            self.update('build', json.load(afile))

        self.update(
            'resources',
            {
                'global': params.global_resources,
            },
        )

        env_file = os.path.join(work_dir, "env.json.txt")
        self.update(
            'internal',
            {
                'env_file': env_file,
                'trace_file': os.path.join(work_dir, const.TRACE_FILE_NAME),
                'core_search_file': os.path.join(work_dir, "core_search.txt"),
            },
        )

        self.update(
            'runtime',
            {
                'gdb_bin': params.gdb_path,
                'output_path': os.path.join(work_dir, const.TESTING_OUT_DIR_NAME),
                'test_tool_path': sys.argv[0],
                'work_path': work_dir,
            },
        )

        if params.retry is not None:
            self.set('runtime', 'retry_index', params.retry)

        if ram_drive_path:
            self.set('runtime', 'ram_drive_path', ram_drive_path)

        if params.split_count > 1:
            self.update(
                'runtime',
                {
                    'split_count': params.split_count,
                    'split_index': params.split_index,
                },
            )

        for opt_name in [
            'build_root',
            'project_path',
            'python_bin',
            'python_lib_path',
            'source_root',
            'split_file',
            'test_params',
        ]:
            val = getattr(params, opt_name)
            if val:
                self.set('runtime', opt_name, val)

    def update(self, section, data):
        self._ctx[section].update(data)

    def set(self, section, key, val):
        self._ctx[section][key] = val

    def get(self, section, key):
        if section not in self._ctx:
            return None

        return self._ctx[section].get(key)

    def save(self, filename=None):
        if filename is None:
            filename = self._filename
        with open(filename, 'w') as afile:
            json.dump(self._ctx, afile, indent=2, sort_keys=True)

        logger.debug("Test context: %s", json.dumps(self._ctx, indent=2, sort_keys=True))

        if self._env:
            self._dump_env()

        return filename

    def _dump_env(self):
        with open(self._ctx["internal"]["env_file"], "a") as file:
            for key, value in six.iteritems(self._env):
                file.write(json.dumps({key: str(value)}, indent=None))
                file.write('\n')

    def update_env(self, env):
        self._env.update(env)
