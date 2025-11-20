# coding: utf-8

import os
import six

from devtools.ya.test import const
import library.python.func
import devtools.ya.core.sec as sec


# TODO get rid
def extend_env_var(env, name, value, sep=":"):
    return sep.join([_f for _f in [env.get(name), value] if _f])


class Environ(object):
    PATHSEP = ':'

    def __init__(self, env=None, only_mandatory_env=False):
        env = os.environ.copy() if env is None else dict(env)
        if only_mandatory_env:
            self._env = {}
        else:
            self._env = env

        self._mandatory = set()
        names = [_f for _f in env.get(const.MANDATORY_ENV_VAR_NAME, '').split(self.PATHSEP) if _f]
        for name in names:
            self._mandatory.add(name)
            if name in env:
                self.set(name, env[name])

    def __setitem__(self, key, value):
        self.set(key, value)

    def __delitem__(self, key):
        del self._env[key]
        if key in self._mandatory:
            self._mandatory.remove(key)

    def __getitem__(self, item):
        return self._env[item]

    def __contains__(self, item):
        return item in self._env

    def __iter__(self):
        return iter(self._env)

    def get(self, name, default=None):
        if default is None:
            return self[name]
        return self._env.get(name, default)

    def set(self, name, value):
        if value is None:
            self.pop(name)
        else:
            self._env[name] = value

    def set_mandatory(self, name, value):
        if value is None:
            self.pop(name)
        else:
            self._env[name] = value
            self._mandatory.add(name)

    def pop(self, name, default=None):
        value = default
        if name in self:
            value = self[name]
            del self[name]
        return value

    def items(self):
        return self._env.items()

    def update(self, data):
        for k, v in six.iteritems(data):
            self.set(k, v)

    def update_mandatory(self, data):
        for k, v in six.iteritems(data):
            self.set_mandatory(k, v)

    def _extend(self, name, val):
        if name in self:
            prefix = self.get(name, '')
        else:
            prefix = os.environ.get(name)

        if prefix:
            return prefix + self.PATHSEP + val
        return val

    def extend(self, name, val):
        self.set(name, self._extend(name, val))

    def extend_mandatory(self, name, val):
        self.set_mandatory(name, self._extend(name, val))

    def adopt_mandatory(self, name):
        if name in os.environ:
            self._env[name] = os.environ[name]
        self._mandatory.add(name)

    def adopt_update_mandatory(self, names):
        for x in names:
            self.adopt_mandatory(x)

    def dump(self, safe=False):
        env = dict(self._env)

        if safe:
            env = sec.environ(env)

        if self._mandatory:
            entries = sorted(set([const.MANDATORY_ENV_VAR_NAME] + list(self._mandatory)))
            env[const.MANDATORY_ENV_VAR_NAME] = self.PATHSEP.join(entries)
        for k, v in env.items():
            assert isinstance(v, six.string_types), (k, v)
        return env

    def clear(self):
        self._env = {}

    def clean_mandatory(self):
        self._mandatory = set()


@library.python.func.lazy
def get_common_env_names():
    env_vars = [name for name in ["SVN_SSH"] if name in os.environ]
    # Work in progress, see YA-365
    skip = (
        "YA_CACHE_DIR",
        "YA_TIMEOUT",
        "YA_TOKEN",
    )
    env_vars += [name for name in os.environ if name.startswith("YA_") and not name.startswith(skip)]
    return env_vars


def get_common_env():
    return Environ({name: os.environ.get(name) for name in get_common_env_names()})


def update_test_initial_env_vars(env, suite, opts):
    env.update(
        {
            "ARCADIA_BUILD_ROOT": "$(BUILD_ROOT)",
            "ARCADIA_ROOT_DISTBUILD": "$(SOURCE_ROOT)",
            "ARCADIA_SOURCE_ROOT": "$(SOURCE_ROOT)",
            "TEST_NAME": suite.name,
            "YA_TEST_RUNNER": "1",
            "GORACE": "halt_on_error=1",
        }
    )

    for san_opts in "LSAN_OPTIONS", "ASAN_OPTIONS", "UBSAN_OPTIONS", "MSAN_OPTIONS":
        env.extend_mandatory(san_opts, "exitcode={}".format(const.SANITIZER_ERROR_RC))
    env.extend_mandatory("UBSAN_OPTIONS", "print_stacktrace=1,halt_on_error=1")
    env.extend_mandatory("MSAN_OPTIONS", "report_umrs=1")

    suite.setup_environment(env, opts)
