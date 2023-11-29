import contextlib


class FuseManager(object):
    def __init__(self):
        pass

    @contextlib.contextmanager
    def do_nothing(self):
        yield

    # We need those arguments to maintain interface
    def manage(self, node, patterns):   # noqa
        return self.do_nothing()


class FuseSandboxing(object):
    def __init__(self, opts, source_root):
        self.opts = opts
        self.source_root = source_root
        self.enable_sandboxing = False

    def manager(self):
        return FuseManager()

    def start(self):
        return

    def stop(self):
        return
