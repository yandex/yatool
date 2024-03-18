import yalibrary.worker_threads as worker_threads  # noqa


class ExpectedNodeException(Exception):
    def __init__(self, exit_code, message):
        self.exit_code = exit_code
        self.args = (message,)
