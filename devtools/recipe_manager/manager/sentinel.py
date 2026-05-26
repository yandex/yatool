import enum
import library.python.retry as lpr
import logging
import multiprocessing as mp
import os
import signal
import subprocess
import time

from devtools.recipe_manager.client import Lifetime

from . import mp_logging
from . import subreaper
from .error import RecipeError, ProcessError, ProcessTimeoutError

logger = logging.getLogger(__name__)


class ChildMethod(enum.StrEnum):
    START_RECIPE = enum.auto()
    STOP_RECIPE = enum.auto()


class Sentinel:
    def __init__(
        self,
        recipe_uid: str,
        lifetime: Lifetime,
        working_dir: str,
    ):
        self._recipe_uid = recipe_uid
        self._lifetime = lifetime
        self._working_dir = working_dir
        self._started_at: float = 0.0

        parent_conn, child_conn = mp.Pipe(duplex=True)
        self._conn = parent_conn
        self._child = mp.Process(
            target=_worker_func,
            name="Recipe sentinel",
            args=(child_conn, mp_logging.get_log_queue(), self._working_dir),
        )

    def start(
        self,
        command: list[str],
        env: dict[str, str],
        err_filename: str,
        out_filename: str,
        timeout: float,
        retry_count: int,
    ):
        assert not self._child.is_alive()
        self._child.start()
        try:
            self._call_child(
                ChildMethod.START_RECIPE, command, env, err_filename, out_filename, timeout or None, retry_count
            )
            self._started_at = time.time()
        except Exception:
            self.abort()
            raise

    def stop(self, command: list[str], env: dict[str, str], err_filename: str, out_filename: str, timeout: float):
        try:
            self._call_child(ChildMethod.STOP_RECIPE, command, env, err_filename, out_filename, timeout or None)
        finally:
            self.abort()

    def abort(self):
        # XXX: Concurrent stop and shutdown may call abort at the same time
        # in different threads. Not sure if it may ever be a problem.
        if self._child.is_alive():
            self._child.terminate()
            self._child.join()

    @property
    def recipe_uid(self) -> str:
        return self._recipe_uid

    @property
    def lifetime(self) -> Lifetime:
        return self._lifetime

    @property
    def working_dir(self) -> str:
        return self._working_dir

    @property
    def started_at(self) -> float:
        """Unix timestamp when the recipe process successfully started. 0.0 if not started yet."""
        return self._started_at

    def _call_child(self, method, *args):
        assert self._child.is_alive()
        self._conn.send((method, args))
        result = self._conn.recv()
        if isinstance(result, Exception):
            raise result
        return result


def _sigterm_handler(signum, frame):
    """Kill all child processes and exit immediately.

    Called when the sentinel worker receives SIGTERM (e.g. from sentinel.abort()).
    Killing children here — before process exit — ensures recipe sub-processes
    (e.g. PostgreSQL) and any daemons they spawned are cleaned up while they are
    still children of this process. This is especially important on macOS where
    there is no subreaper mechanism: without this handler orphaned grandchildren
    would be reparented to launchd and never killed.

    We use os._exit(0) instead of sys.exit(0) because sys.exit(0) triggers a
    clean Python shutdown, which waits for all non-daemon threads to complete.
    The worker process has a logging QueueFeeder thread (from mp_logging) that
    may be blocked on condition.wait() when the logging queue is idle. This thread
    prevents shutdown from completing, leaving sys.exit(0) hanging forever.
    Since kill_children() is called explicitly here, we don't need atexit handlers,
    and os._exit(0) bypasses the deadlock entirely.
    """
    subreaper.kill_children()
    os._exit(0)


def _worker_func(conn, log_queue, working_dir):
    # Install SIGTERM handler before setup_cronus so that even if atexit is
    # skipped (os._exit path) children are still cleaned up.
    signal.signal(signal.SIGTERM, _sigterm_handler)
    subreaper.setup_cronus()
    os.chdir(working_dir)
    mp_logging.init_child_logger(log_queue)
    global logger
    logger = logging.getLogger(__name__)
    logger.debug("Sentinel worker has started")
    try:
        while call := conn.recv():
            method, args = call
            try:
                result = _process_call(method, args)
                conn.send(result)
            except RecipeError as e:
                conn.send(e)
            except Exception as e:
                logger.exception(f"{method}() failed")
                conn.send(RecipeError(str(e)))
    except EOFError:
        logger.debug("Pipe is closed by parent")
    # XXX: For the same reason as in _sigterm_handler
    os._exit(0)


def _process_call(method: ChildMethod, args: list):
    if method == ChildMethod.START_RECIPE:
        return _run_command(*args)
    elif method == ChildMethod.STOP_RECIPE:
        return _run_command(*args)
    else:
        raise RuntimeError("Unknown method: {method}")


def _run_command(
    command: list[str], env: dict[str, str], err_filename: str, out_filename: str, timeout: float, retry_count: int = 0
):
    def log_error(e: Exception, n: int, raise_after) -> None:
        logger.debug(
            "Run (attempt {} of {}): '{}' failed with error: '{}'".format(n, retry_count + 1, " ".join(command), e)
        )

    with open(err_filename, "ab") as err, open(out_filename, "ab") as out:
        retry_conf = lpr.RetryConf(
            retriable=lambda e: isinstance(e, (subprocess.CalledProcessError, subprocess.TimeoutExpired)),
            max_times=retry_count,
            handle_error=log_error,
        )
        logger.debug("Run: {}".format(" ".join(command)))
        try:
            lpr.retry_call(
                lambda: subprocess.check_call(command, env=env, stderr=err, stdout=out, timeout=timeout or None),
                conf=retry_conf,
            )
            logger.debug("Run successfully completed: {}".format(" ".join(command)))
        except subprocess.TimeoutExpired as e:
            raise ProcessTimeoutError(str(e))
        except subprocess.CalledProcessError as e:
            raise ProcessError(str(e))
