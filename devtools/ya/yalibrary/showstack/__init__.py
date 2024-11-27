import threading
import traceback
import sys
import signal
from io import StringIO


def format_stack(my_frame):
    out = StringIO()
    for th in threading.enumerate():
        if th.ident != threading.current_thread().ident:
            frame = sys._current_frames()[th.ident]
        else:
            frame = my_frame
        traceback.print_stack(frame, file=out)
        out.write('\n')

    return out.getvalue()


def configure_show_stack_on_signal(sig):
    def sig_handler(sig, frame):
        sys.stderr.write(format_stack(frame))

    signal.signal(sig, sig_handler)
    try:
        yield
    finally:
        signal.signal(sig, signal.SIG_DFL)
