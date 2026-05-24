import argparse
import contextlib
import faulthandler
import logging
import multiprocessing as mp
import os
import signal
import sys
import threading
from library.python.filelock import FileLock
from library.python.fs import remove_tree_safe

from . import mp_logging
from . import service
from . import subreaper
from devtools.recipe_manager.client.client import get_shallow_root_meta_path, get_shallow_root_data_path

logger = logging.getLogger(__name__)


class _ManagerFileLock(FileLock):
    def acquire(self):
        if not super().acquire(blocking=False):
            logger.error("Another instance of the recipe manager is already running")
            sys.exit(1)


def _daemonize():
    if os.fork() > 0:
        os._exit(0)
    os.setsid()
    if os.fork() > 0:
        os._exit(0)

    sys.stdout.flush()
    sys.stderr.flush()
    with contextlib.ExitStack() as stack:
        si = stack.enter_context(open(os.devnull, 'rb'))
        so = stack.enter_context(open(os.devnull, 'ab+'))
        se = stack.enter_context(open(os.devnull, 'ab+', 0))
        os.dup2(si.fileno(), sys.stdin.fileno())
        os.dup2(so.fileno(), sys.stdout.fileno())
        os.dup2(se.fileno(), sys.stderr.fileno())


def main():
    mp.set_start_method("spawn")
    mp.freeze_support()
    os.environ.pop("YA_PYTHON_ENTRY_POINT", None)

    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument("--shallow-root", required=True)
    arg_parser.add_argument(
        "--log-file",
        required=True,
        help="Log file name. Specify '-' for logging to stderr",
    )
    arg_parser.add_argument("--daemonize", action="store_true")
    args = arg_parser.parse_args()

    if args.log_file == "-" and args.daemonize:
        print("In daemon mode, outputting logs to the console is not possible", file=sys.stderr)
        sys.exit(1)

    if args.daemonize:
        _daemonize()

    subreaper.setup_cronus()
    faulthandler.register(signal.SIGQUIT)

    with contextlib.ExitStack() as exit_stack:
        exit_stack.enter_context(mp_logging.init_parent_logger(args.log_file))

        shallow_root = os.path.expanduser(args.shallow_root)

        # meta/ is never deleted — holds lock and version files, and the socket.
        meta_dir = get_shallow_root_meta_path(shallow_root)
        os.makedirs(meta_dir, exist_ok=True)
        lock_file_path = os.path.join(meta_dir, "manager.lock")
        exit_stack.enter_context(_ManagerFileLock(lock_file_path))

        # data/ holds recipe working dirs — clean up leftovers from a previous RM.
        data_dir = get_shallow_root_data_path(shallow_root)
        os.makedirs(data_dir, exist_ok=True)
        for name in os.listdir(data_dir):
            remove_tree_safe(os.path.join(data_dir, name))

        stop_event = threading.Event()
        signal.signal(signal.SIGTERM, lambda _, __: stop_event.set())
        signal.signal(signal.SIGINT, lambda _, __: stop_event.set())
        try:
            service.serve(shallow_root, stop_event)
        except BaseException:
            logger.exception("Server failed")
        finally:
            logger.debug("Server has finished")
