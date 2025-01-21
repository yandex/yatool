import logging
import os
import sys

import yalibrary.find_root
import exts.process

import devtools.ya.core.config

RESPAWNS_PARAM = 'YA_RESPAWNS'

# If you enable this, respawn to ya2 will add `-2` to command line for ya script
# and can break backward compatibility with ya scripts older than https://a.yandex-team.ru/arcadia/commit/r10186426
SPECIFY_YA_2_WHEN_RESPAWN = False

logger = logging.getLogger(__name__)
logger_pyver = logging.getLogger(__name__ + ".pyver")


class IncompatiblePythonMajorVersion(Exception):
    pass


def _get_current_respawns():
    return [_f for _f in os.environ.get(RESPAWNS_PARAM, "").split(os.pathsep) if _f]


def _create_respawn_env(env, respawns):
    if respawns:
        env[RESPAWNS_PARAM] = os.pathsep.join(respawns)
    return env


def _get_new_respawns(reasons):
    return [r for r in reasons if r and r not in _get_current_respawns()]


def _src_root_respawn(arc_dir):
    if os.environ.get('YA_NO_RESPAWN'):
        logger.debug('Skipping respawn by YA_NO_RESPAWN variable')
        return

    py_path = sys.executable
    target = None
    for t in [os.path.join(arc_dir, 'ya'), os.path.join(arc_dir, 'devtools', 'ya', 'ya')]:
        if os.path.exists(t):
            target = t
            break

    if target is None:
        raise Exception('Cannot find ya in ' + arc_dir)

    reasons = [target]

    # Check here for respawn
    new_respawns = _get_new_respawns(reasons)
    if not new_respawns:
        logger.debug("New reasons for respawn not found, skip")
        return

    # Specify ya-bin version for ya-bin3
    ya_script_prefix = []

    cmd = [target] + ya_script_prefix + sys.argv[1:]

    env = _create_respawn_env(os.environ.copy(), _get_current_respawns() + new_respawns)
    env['YA_SOURCE_ROOT'] = arc_dir
    env['Y_PYTHON_ENTRY_POINT'] = ':main'

    # -E     : ignore PYTHON* environment variables (such as PYTHONPATH)
    # -s     : don't add user site directory to sys.path; also PYTHONNOUSERSITE
    # -S     : don't imply 'import site' on initialization
    full_cmd = ["-E", "-s", "-S"] + cmd
    logger.debug('Respawn %s %s (triggered by: %s)', py_path, ' '.join(full_cmd), new_respawns)

    exts.process.execve(py_path, full_cmd, env)


class EmptyValue:
    pass


def check_for_respawn(new_root):
    try:
        prev_root = devtools.ya.core.config.find_root(fail_on_error=False)

        root_change = prev_root != new_root

        if not (root_change):
            logger.debug('Same as prev source root %s', new_root)
            logger_pyver.debug("No need to respawn to other ya-bin version")
            return

        if root_change:
            logger.debug('New source root %s is not the same as prev %s', new_root, prev_root)

        entry_root = yalibrary.find_root.detect_root(devtools.ya.core.config.entry_point_path())

        entry_root_change = entry_root != new_root

        if entry_root_change:
            _src_root_respawn(arc_dir=new_root)

    finally:
        # It's needed only when a respawn happens.
        logger.debug('Unsetting YA_STDIN env var as respawn never happened')
        if 'YA_STDIN' in os.environ:
            del os.environ['YA_STDIN']


def filter_env(env_vars):
    copy_vars = dict(env_vars)
    copy_vars.pop(RESPAWNS_PARAM, None)
    return copy_vars
