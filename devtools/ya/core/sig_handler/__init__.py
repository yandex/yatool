import os
import signal
import sys

import termcolor


if sys.stderr.isatty():
    STOP_STR = termcolor.colored('\r' + chr(27) + '[2KStop\n', color='red', attrs=['bold'])
else:
    STOP_STR = 'Stop\n'


def instant_sigint_exit_handler(*args):
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    os.kill(0, signal.SIGINT)

    try:
        sys.stderr.write(STOP_STR)
    except Exception:
        pass

    # Force exit without processing exit handlers
    os._exit(-signal.SIGINT)
