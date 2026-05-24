import contextlib
import logging
import logging.handlers
import multiprocessing as mp
import sys

log_queue = None


@contextlib.contextmanager
def init_parent_logger(log_file_path):
    global log_queue
    if log_file_path == "-":
        log_handler = logging.StreamHandler(sys.stderr)
    else:
        log_handler = logging.FileHandler(log_file_path)

    log_handler.setLevel(logging.DEBUG)
    log_handler.setFormatter(
        logging.Formatter("%(asctime)s %(levelname)s (%(name)s) [%(process)d-%(threadName)s] %(message)s")
    )

    log_queue = mp.Queue()
    queue_listener = logging.handlers.QueueListener(log_queue, log_handler)
    queue_listener.start()

    init_child_logger(log_queue)

    # Note: in Python 3.14 QueueListener can be used as a context manager, but in 3.13 this is not possible.
    try:
        yield
    finally:
        queue_listener.stop()


def get_log_queue():
    assert log_queue
    return log_queue


def init_child_logger(log_queue):
    root_logger = logging.getLogger()
    root_logger.setLevel(logging.DEBUG)
    root_logger.addHandler(logging.handlers.QueueHandler(log_queue))
