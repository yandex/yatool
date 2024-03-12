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
    sys.dont_write_bytecode = True

    from entry.entry import main

    main(sys.argv)


if __name__ == "__main__":
    main()
