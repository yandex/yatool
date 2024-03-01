from __future__ import absolute_import

import gc
import os
import sys
import time
import core.stage_tracer as stage_tracer

stager = stage_tracer.get_tracer("overall-execution")


def _init_tracer():
    last_environ_stage_tstamp = time.time()
    stages = os.environ.get('YA_STAGES', '')
    if stages:
        prev_stage = None
        for stage in stages.split(":"):
            name, tstamp = stage.split("@")
            tstamp = float(tstamp)
            if prev_stage is not None:
                prev_stage.finish(tstamp)
            prev_stage = stager.start(name, tstamp)

        if prev_stage is not None:
            prev_stage.finish(last_environ_stage_tstamp)

    return stager


def main():
    _init_tracer()
    stager.start("main-processing")
    gc.set_threshold(0)
    sys.dont_write_bytecode = True

    from entry.entry import main

    main(sys.argv)
