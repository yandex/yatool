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
                with open(STAGES_FILE) as afile:
                    data = json.load(afile)
            except IOError:
                data = {}
            data[k] = v
            with open(STAGES_FILE, 'w') as afile:
                json.dump(data, afile, indent=4, sort_keys=True)


def _get_value(stage):
    if not STAGES_FILE:
        return

    try:
        with open(STAGES_FILE) as afile:
            data = json.load(afile)
        return data[stage]
    except (IOError, KeyError):
        return


def stage_started(name, ts=None):
    _set_value(name + '_started', ts or time.time())


def stage_finished(name, ts=None):
    _set_value(name + '_finished', ts or time.time())


def get_stage_timestamps(name):
    started = _get_value(name + '_started')
    finished = _get_value(name + '_finished')
    return started, finished


def load_stages(stages, prefix=''):
    if prefix:
        prefix += '_'
    for k, v in iteritems(stages):
        _set_value(prefix + k, v)
