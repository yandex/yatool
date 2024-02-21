import exts.yjson as json
import logging
import multiprocessing
import os
import time
import collections

from six import iteritems

from exts import func
from exts import fs


logger = logging.getLogger(__name__)


PROFILE_FILE = None

_CUTOFF = 0
_DATA = collections.defaultdict(dict)


@func.lazy
def _profile_lock():
    return multiprocessing.Lock()


def clear(to_file):
    # XXX: to avoid redefinition
    if to_file is None:
        return

    global PROFILE_FILE
    PROFILE_FILE = to_file
    if PROFILE_FILE is None:
        return

    if os.path.exists(PROFILE_FILE):
        os.remove(PROFILE_FILE)
    global _CUTOFF
    _CUTOFF = time.time()
    _set_value('step', 'start_time', _CUTOFF)
    _set_value('step', 'ya_started', 0)
    _flush()


def _get_value(namespace, key, default=None):
    if (namespace not in _DATA) or (key not in _DATA[namespace]):
        return default

    return _DATA[namespace][key]


def _flush():
    if PROFILE_FILE is not None:
        fs.write_file(PROFILE_FILE, json.dumps(_DATA, indent=4, sort_keys=True))


def _set_value(namespace, key, value):
    _DATA[namespace][key] = value
    return _DATA[namespace]


def profile_step(step_name, step_time=None):
    step_time = step_time or time.time()
    value = step_time - _CUTOFF

    logger.debug('Profile step {} - {}'.format(step_name, value))

    with _profile_lock():
        _set_value('step', step_name, value)

    _flush()


def profile_step_started(step_name, step_time=None):
    profile_step(step_name + '_started', step_time)


def profile_step_finished(step_name, step_time=None):
    profile_step(step_name + '_finished', step_time)


def profile_value(value_name, value):
    logger.debug('Profile value {} - {}'.format(value_name, value))

    with _profile_lock():
        _set_value('value', value_name, value)

    _flush()


def profile_inc_value(value_name, delta=1):
    with _profile_lock():
        _set_value('value', value_name, delta + _get_value('value', value_name, 0))


def load_profile(profile, prefix=''):
    if prefix:
        prefix += '_'
    for namespace, data in iteritems(profile):
        if namespace != 'step':
            for name, value in iteritems(data):
                _set_value(namespace, prefix + name, value)
    steps = profile.get('step', {})
    for name, value in iteritems(steps):
        _set_value('step', prefix + name, value + steps.get('start_time', 0) - _CUTOFF)
