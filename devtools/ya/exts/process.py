import enum
import logging
import os
import subprocess
import sys
import threading

from library.python.strings import ensure_str_deep
from library.python import windows
import six


class TimeoutExpired(Exception):
    def __init__(self, stdout='', stderr=''):
        self.stdout = stdout
        self.stderr = stderr


logger = logging.getLogger(__name__)


class ControlTransferType(enum.Enum):
    EXEC = 'exec'
    RESPAWN = 'respawn'


_pre_execve_hooks = []  # type: list
_pre_exec_observers = []  # type: list


def register_pre_execve_hook(hook):
    # type: (callable) -> None
    _pre_execve_hooks.append(hook)


def register_pre_exec_observer(observer):
    # type: (callable) -> None
    _pre_exec_observers.append(observer)


def unregister_pre_exec_observer(observer):
    # type: (callable) -> None
    try:
        _pre_exec_observers.remove(observer)
    except ValueError:
        pass


def notify_pre_exec(
    exec_path,
    args,
    method,
    cwd=None,
    transfer_type=ControlTransferType.EXEC,
    transfer_details=None,
):
    if isinstance(transfer_type, ControlTransferType):
        transfer_type = transfer_type.value

    transfer = {
        'schema_version': 1,
        'status': 'initiated',
        'transfer_type': transfer_type,
        'method': method,
        'executable': exec_path,
        'args': list(args),
        'cwd': cwd or os.getcwd(),
        'details': transfer_details or {},
    }
    for observer in _pre_exec_observers:
        try:
            observer(transfer)
        except Exception:
            logger.debug('Pre-exec observer failed', exc_info=True)

    _run_pre_execve_hooks()


def _run_pre_execve_hooks():
    for hook in _pre_execve_hooks:
        try:
            hook()
        except Exception:
            logger.debug('Pre-execve hook failed', exc_info=True)


# Wrapper for Popen with some fixes and improvements
@windows.errorfix
def popen(*args, **kwargs):
    if windows.on_win():
        windows.disable_error_dialogs()
        if 'creationflags' not in kwargs:
            kwargs['creationflags'] = windows.default_process_creation_flags()
    return subprocess.Popen(*args, **kwargs)


# function below uses special code for win instead of os.execve because of: https://bugs.python.org/issue9148
def execve(
    exec_path,
    args=[],
    env=None,
    cwd=None,
    transfer_type=ControlTransferType.EXEC,
    transfer_details=None,
):
    from library.python import tmp

    tmp.remove_tmp_dirs(env)

    if env is None:
        env = os.environ

    if not windows.on_win():
        notify_pre_exec(
            exec_path,
            [exec_path] + args,
            method='execve',
            cwd=cwd,
            transfer_type=transfer_type,
            transfer_details=transfer_details,
        )
        logging.shutdown()
        if cwd:
            os.chdir(cwd)
        os.execve(exec_path, [exec_path] + args, env)  # after this call no return in current process
        assert False, 'WTF: no return from os.execve: {}.'.format([exec_path] + args)

    _run_pre_execve_hooks()
    logging.shutdown()
    if env:
        env = ensure_str_deep(env)
    spr = popen([exec_path] + args, env=env, cwd=cwd)  # emulate exec for win
    spr.wait()
    sys.exit(spr.returncode)


def execvp(exec_path, args):
    notify_pre_exec(exec_path, args, method='execvp')
    os.execvp(exec_path, args)


def run_process(exec_path, args=[], env=None, cwd=None, check=False, pipe_stdout=True, return_stderr=False):
    logger.debug("run %s with args %s and %s env", exec_path, args, env)
    process = popen(
        [exec_path] + args, stdout=subprocess.PIPE if pipe_stdout else None, stderr=subprocess.PIPE, env=env, cwd=cwd
    )
    output, errors = process.communicate()
    if check and process.returncode:
        logger.error(errors)
        raise subprocess.CalledProcessError(process.returncode, [exec_path] + args, output=output)
    if isinstance(output, bytes):
        output = output.decode("ascii")
    return output if not return_stderr else (output, errors)


# Legacy, use exts.process.popen wrapper
def subprocess_flags():
    if windows.on_win():
        windows.disable_error_dialogs()
        return windows.default_process_creation_flags()
    return 0


def set_close_on_exec(stream):
    if windows.on_win():
        windows.set_handle_information(stream, inherit=False)
    else:
        import fcntl

        flags = fcntl.fcntl(stream, fcntl.F_GETFD)
        flags |= fcntl.FD_CLOEXEC
        fcntl.fcntl(stream, fcntl.F_SETFD, flags)


def wait_for_proc(proc, timeout=None):
    if timeout is None:
        return proc.communicate()

    res, err = [], []

    def run():
        try:
            res.extend(proc.communicate())
        except Exception:
            err.extend(sys.exc_info())

    th = threading.Thread(target=run)
    th.daemon = True
    th.start()

    th.join(timeout)
    alive = th.is_alive()
    proc.terminate()
    th.join()

    if err:
        six.reraise(err[0], err[1], err[2])
    if alive:
        raise TimeoutExpired(res[0], res[1])
    return res[0], res[1]


def find_opened_file_across_all_procs(filepath):
    import psutil

    for proc in psutil.process_iter():
        try:
            for f in proc.open_files():
                if f.path == filepath:
                    yield f, proc
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass


def is_process_in_subtree(child_pid, parent_pid):
    import psutil

    parent = psutil.Process(parent_pid)

    for child in parent.children(recursive=True):

        if child.pid == child_pid:
            return True

    return False
