"""Bounded OS service-controller commands shared by platform adapters."""

import os
import subprocess
import tempfile
import time
from typing import Callable

from exts import process as processes
from devtools.ya.build.larry_service import manager


def run_command(argv: tuple[str, ...], timeout: float, check_cancel: Callable[[], None]) -> str:
    """Bound the controller lifetime without claiming to undo submitted jobs.

    exts.process.popen supplies platform fixes; its wait helpers do not combine
    application cancellation, output limits and reaping. Never echo raw output.
    """
    name = os.path.basename(argv[0])
    check_cancel()
    with tempfile.TemporaryFile() as output:
        try:
            process = processes.popen(
                argv,
                stdin=subprocess.DEVNULL,
                stdout=output,
                stderr=subprocess.DEVNULL,
            )
        except OSError:
            raise manager.ServiceManagerError(f'Cannot execute {name}; a user service manager is required') from None
        try:
            deadline = time.monotonic() + timeout
            while process.poll() is None:
                check_cancel()
                if time.monotonic() >= deadline:
                    raise manager.ServiceManagerError(f'{name} {argv[1]} timed out; the submitted job may continue')
                time.sleep(min(0.05, max(0, deadline - time.monotonic())))
            check_cancel()
            if process.returncode:
                raise manager.ServiceManagerError(f'{name} {argv[1]} failed; check the user service manager')
            output.seek(0)
            data = output.read(64 * 1024 + 1)
            if len(data) > 64 * 1024:
                raise manager.ServiceManagerError(f'{name} output exceeds the limit')
            try:
                return data.decode('utf-8')
            except UnicodeError:
                raise manager.ServiceManagerError(f'Invalid {name} output') from None
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
