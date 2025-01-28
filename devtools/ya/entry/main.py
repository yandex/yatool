from __future__ import absolute_import

import gc
import os
import sys
import time


def add_stage_start_to_environ(stage_name):
    stages = os.environ.get('YA_STAGES', '')
    os.environ['YA_STAGES'] = stages + (':' if stages else '') + '{}@{}'.format(stage_name, time.time())


def main():
    add_stage_start_to_environ("main-processing")
    gc.set_threshold(0)

    fast_exit = os.environ.get('YA_FAST_EXIT') in ('yes', '1')
    exit_code = 0

    if fast_exit:
        import atexit

        def _fast_exit():
            # Flush standard streams and exit rapidly to avoid long Python finalization
            # At this point all useful work is done
            sys.stdout.flush()
            sys.stderr.flush()
            os._exit(exit_code)

        # Libraries can legally use the atexit module, so to ensure that our function is called last,
        # we should register it as early as possible.
        atexit.register(_fast_exit)

    from entry.entry import main

    try:
        main(sys.argv)
    except SystemExit as e:
        exit_code = e.code
        raise


if __name__ == "__main__":
    main()
