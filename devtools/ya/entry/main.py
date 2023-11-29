from __future__ import absolute_import

import gc
import sys


def main():
    gc.set_threshold(0)
    sys.dont_write_bytecode = True

    from entry.entry import main

    main(sys.argv)
