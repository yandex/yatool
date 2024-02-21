import os
import time
import exts.yjson as json
import logging
import multiprocessing

from six import iteritems

from exts import func

logger = logging.getLogger(__name__)

STAGES_FILE = None


@func.lazy
def _lock():
    return multiprocessing.Lock()


def clear(to_file):
    if to_file is None:
        return

    global STAGES_FILE
    STAGES_FILE = to_file

    if os.path.exists(STAGES_FILE):
        os.remove(STAGES_FILE)


def _set_value(k, v):
    logger.debug('Set stage %s=%s', str(k), str(v))
    if STAGES_FILE:
        with _lock():
            try:
                data = json.load(open(STAGES_FILE))
            except IOError:
                data = {}
            data[k] = v
            json.dump(data, open(STAGES_FILE, 'w'), indent=4, sort_keys=True)


def stage_started(name, ts=None):
    _set_value(name + '_started', ts or time.time())


def stage_finished(name, ts=None):
    _set_value(name + '_finished', ts or time.time())


def load_stages(stages, prefix=''):
    if prefix:
        prefix += '_'
    for k, v in iteritems(stages):
        _set_value(prefix + k, v)
