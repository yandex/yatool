import logging
import os
import subprocess
import tempfile
import time
import traceback

import six

from . import util

import ujson as json
import iter_cov_json
from library.python.reservoir_sampling import reservoir_sampling


logger = logging.getLogger(__name__)


class DiscreetReader(object):
    class ShutDownException(Exception):
        pass

    def __init__(self, cmd, filename, cancel_func=None):
        self.cmd = cmd
        self.filename = filename
        self.read_file = None
        self.write_file = None
        self.pid = None
        self.rusage = {}
        self.proc = None
        self.cancel_func = cancel_func

    def __enter__(self):
        self.write_file = open(self.filename, "w")
        self.read_file = open(self.filename, "rb")
        logger.debug("Executing: %s", self.cmd)
        self.proc = subprocess.Popen(self.cmd, stdout=self.write_file)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type != DiscreetReader.ShutDownException:
            if exc_type:
                logger.debug("Exception: %s", "".join(traceback.format_exception(exc_type, exc_val, exc_tb)))
            else:
                assert self.proc.returncode == 0, self.proc.returncode
        self.read_file.close()
        self.write_file.close()

    def wait(self, flags=0):
        if six.PY2:
            pid, status, rusage = subprocess._eintr_retry_call(os.wait4, self.proc.pid, flags)
        else:
            pid, status, rusage = os.wait4(self.proc.pid, flags)
        if pid > 0:
            self.proc.returncode = status
            self.rusage = rusage
            return status

        return None

    def read(self, size):
        while True:
            data = self.read_file.read(size)

            if self.cancel_func and self.cancel_func():
                logger.warning("Shutdown requested")
                self.proc.terminate()
                self.wait()
                raise DiscreetReader.ShutDownException()

            if data:
                return data

            if self.wait(os.WNOHANG) is None:
                time.sleep(0.5)
            else:
                # The data might be written by the process between our read attempt
                # and process status check (when wait(WNOHANG) is not None)
                return self.read_file.read(size)


def export_llvm_coverage(cmd, process_func, output_filename=None, cancel_func=None):
    output_filename = output_filename or tempfile.NamedTemporaryFile(delete=False).name
    block_sizes = []
    nfiles = 0

    discreet_reader = DiscreetReader(cmd, output_filename, cancel_func)
    try:
        with discreet_reader as afile:
            for target_block, block in iter_cov_json.iter_coverage_json(afile):
                # skip irrelevant data
                if not target_block:
                    continue

                if not block:
                    logger.debug("Empty entry: %s", target_block)
                    continue

                covtype = target_block[-1]
                filename = util.guess_llvm_coverage_filename(covtype, block)

                block_sizes.append((filename, covtype, len(block)))

                if covtype == "files":
                    nfiles += 1

                process_func(covtype, filename, block)
    except DiscreetReader.ShutDownException:
        logger.debug("Failed to export coverage - shutdown was requested")
        raise
    finally:
        logger.debug("%s rusage: %s", cmd[0], discreet_reader.rusage)

    logger.debug(
        "Exported coverage size: %dkb. Coverage file entries: %d", os.stat(output_filename).st_size // 1024, nfiles
    )
    sorted_sized = sorted(block_sizes, key=lambda x: x[2], reverse=True)[:20]
    logger.debug(
        "Top 20 block sizes:\n%s",
        '\n'.join("  {}b {} {}".format(size, path, covtype) for path, covtype, size in sorted_sized),
    )


def dump_coverage(coverage, dst, nsamples=10):
    samples = sorted(reservoir_sampling(coverage, nsamples))
    logger.debug("Dumping coverage (%d entries) samples (%d):\n%s", len(coverage), len(samples), "\n".join(samples))
    with open(dst, 'w') as afile:
        for filename, covdata in coverage.items():
            json.dump({'filename': filename, 'coverage': covdata}, afile)
            afile.write('\n')
