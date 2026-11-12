"""Manage a persistent per-user Larry through systemd, never through RPC."""

import math
import os
import re
import time
import typing
from typing import Callable

from devtools.ya.build.larry_service import (
    configuration,
    controller,
    installation,
    manager,
    paths as service_paths,
    release,
    state_files,
)


class ManagerTimeouts(typing.NamedTuple):
    command: float = 45.0
    ready: float = 30.0
    stop: float = 20.0


def run_systemctl(args: tuple[str, ...], timeout: float, check_cancel: Callable[[], None]) -> str:
    return controller.run_command(
        ('systemctl', '--user', '--no-pager', '--no-ask-password', *args), timeout, check_cancel
    )


def process_start_time(pid: int) -> int:
    """Read Linux start ticks, matching TProcessUID's readiness representation.

    psutil's epoch seconds lose the native identity units. The snowden helper
    is private to that daemon; keep the Linux-specific probe in this adapter.
    """
    try:
        with open(f'/proc/{pid}/stat') as source:
            fields = source.read().rsplit(')', 1)[1].split()
        return 0 if fields[0] in ('Z', 'X', 'x') else int(fields[19])
    except (OSError, IndexError, ValueError):
        return 0


def _quote(value):
    if not value or any(ord(char) < 32 or ord(char) == 127 for char in value):
        raise ValueError('Unit arguments must be nonempty text without control characters')
    # systemd expands specifiers and environment variables after tokenization.
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') + '"'


def render_unit(launch: configuration.ServiceLaunch, timeouts: ManagerTimeouts) -> bytes:
    if not all(os.path.isabs(path) for path in (launch.executable, launch.configuration)):
        raise ValueError('Service launch paths must be absolute')
    # Executable lookup does not perform environment expansion; arguments do.
    arguments = ' '.join(_quote(value.replace('$', '$$')) for value in launch.argv[1:])
    command = f'{_quote(launch.executable)} {arguments}'
    return (
        '[Unit]\nDescription=Managed Larry build service\nStartLimitIntervalSec=60\nStartLimitBurst=3\n'
        '[Service]\nType=exec\n'
        f'ExecStart={command}\n'
        f'TimeoutStartSec={timeouts.ready:g}\nTimeoutStopSec={timeouts.stop:g}\n'
        'KillMode=control-group\nSendSIGKILL=yes\nRestart=on-failure\nRestartSec=2\n'
        'StandardOutput=journal\nStandardError=journal\nUMask=0077\n'
    ).encode('utf-8')


class SystemdServiceManager:
    """Own only the user-manager adapter; recovery transactions belong to ya.

    Callers serialize selection/installation/restart and retain installations
    while they can still be launched. This class neither selects releases nor
    imports client environment into the manager.
    """

    def __init__(
        self,
        paths: service_paths.ServicePaths,
        *,
        unit_name: str = 'ya-larry.service',
        timeouts: ManagerTimeouts = ManagerTimeouts(),
        command_runner: Callable = run_systemctl,
        identity_reader: Callable[[int], int] = process_start_time,
        check_cancel: Callable[[], None] = release._check_cancel,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if any(not math.isfinite(value) or value <= 0 for value in timeouts):
            raise ValueError('Manager timeouts must be finite and positive')
        if timeouts.command <= timeouts.stop:
            raise ValueError('Command timeout must exceed the stop timeout')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.service', unit_name):
            raise ValueError('Expected a plain systemd service unit name')
        self.paths = paths
        self.unit_name = unit_name
        self._timeouts = timeouts
        self._run = command_runner
        self._identity = identity_reader
        self._check_cancel = check_cancel
        self._clock = clock
        self._sleep = sleep

    def _command(self, *args, timeout=None):
        self._check_cancel()
        return self._run(tuple(args), self._timeouts.command if timeout is None else timeout, self._check_cancel)

    def ensure_installed(self, launch: configuration.ServiceLaunch) -> None:
        contents = render_unit(launch, self._timeouts)
        with installation._locked(self.paths, self._check_cancel):
            path = os.path.join(os.path.realpath(self.paths.root), self.unit_name)
            try:
                with open(path, 'rb') as source:
                    unchanged = source.read() == contents
            except FileNotFoundError:
                unchanged = False
            if not unchanged:
                state_files.write_private_file(path, contents, check_cancel=self._check_cancel)
            # Reconcile registration even after interruption between write/link/reload.
            # No --force: a conflicting user unit must not be silently overwritten.
            self._command('link', path)
            self._command('daemon-reload')

    def _status(self, timeout):
        fields = ('LoadState', 'ActiveState', 'SubState', 'MainPID', 'InvocationID')
        output = self._command('show', '--property=' + ','.join(fields), self.unit_name, timeout=timeout)
        try:
            values = dict(line.split('=', 1) for line in output.splitlines() if line)
            pid = int(values['MainPID'])
            if pid < 0 or any(not values[field] for field in fields[:3]):
                raise ValueError()
            return manager.ServiceStatus(
                values['LoadState'], values['ActiveState'], values['SubState'], pid, values['InvocationID']
            )
        except (KeyError, ValueError):
            raise manager.ServiceManagerError('Invalid systemd service status') from None

    def status(self) -> manager.ServiceStatus:
        return self._status(self._timeouts.command)

    def start(self) -> None:
        self._command('start', self.unit_name)

    def restart(self) -> None:
        self._command('restart', self.unit_name)

    def wait_ready(self, expected: manager.ReadinessExpectation) -> manager.ReadyInstance:
        deadline = self._clock() + self._timeouts.ready
        while True:
            self._check_cancel()
            remaining = deadline - self._clock()
            if remaining <= 0:
                raise manager.ServiceNotReadyError(f'Readiness timed out for {self.unit_name}')
            status = self._status(min(remaining, self._timeouts.command))
            if status.load_state != 'loaded' or status.active_state == 'failed':
                raise manager.ServiceNotReadyError(
                    f'Service is not runnable; inspect journalctl --user -u {self.unit_name}'
                )
            if (
                status.active_state == 'active'
                and status.sub_state == 'running'
                and status.pid
                and status.invocation_id
            ):
                record = manager.read_ready_instance(self.paths, status, expected)
                if record and self._identity(status.pid) == record.process_start_time:
                    remaining = deadline - self._clock()
                    if remaining > 0 and self._status(min(remaining, self._timeouts.command)) == status:
                        if self._identity(status.pid) == record.process_start_time:
                            self._check_cancel()
                            if self._clock() < deadline:
                                return record
            self._sleep(min(0.1, max(0, deadline - self._clock())))
