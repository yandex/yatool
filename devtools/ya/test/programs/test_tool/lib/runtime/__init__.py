import signal
import contextlib
import os


def get_maxrss():
    try:
        import resource
    except ImportError:
        return 0
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss


@contextlib.contextmanager
def bypass_signals(signames):
    pids = []

    def bypass_handler(s, _):
        for p in pids:
            try:
                os.kill(p, s)
            except Exception:
                pass

    class _ProcRegister:
        def register(self, pid):
            pids.append(pid)

    orig_handlers = []

    for signame in set(signames):
        if not hasattr(signal, signame):
            continue

        sig = getattr(signal, signame)
        try:
            orig_handlers.append((sig, signal.getsignal(sig)))
        except Exception:
            continue

        signal.signal(sig, bypass_handler)

    yield _ProcRegister()

    for sig, handler in orig_handlers:
        if handler:
            signal.signal(sig, handler)
        else:
            signal.signal(sig, signal.SIG_DFL)
