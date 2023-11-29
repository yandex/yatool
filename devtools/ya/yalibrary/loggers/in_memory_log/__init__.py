import logging


class InMemoryLogger(logging.Handler):
    def __init__(self):
        logging.Handler.__init__(self)
        self.closed = False
        self.storage = []

    def close(self):
        # Py2 lists do not have clear() method, so using this :(
        del self.storage[:]  # XXX
        self.closed = True

    def emit(self, record):
        if not self.closed:
            self.storage.append(record)


def with_in_memory_log(level):
    root = logging.getLogger()
    root.setLevel(level)

    handler = InMemoryLogger()
    # logging.INFO is a default mode for display
    # We set this handler to store only WARNING to output no excess data on display.
    if level == logging.INFO:
        handler.setLevel(logging.WARNING)
    else:
        handler.setLevel(level)

    root.addHandler(handler)
    yield handler
