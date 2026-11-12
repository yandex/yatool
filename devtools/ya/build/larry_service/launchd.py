"""Manage Larry in the invoking user's macOS GUI launchd domain."""

import json
import math
import os
import plistlib
import re
import time
import typing
from typing import Callable

from devtools.ya.build.larry_service import configuration, controller, installation, manager, release, state_files


class _StatusDeadlineError(manager.ServiceManagerError):
    pass


class ManagerTimeouts(typing.NamedTuple):
    command: float = 45.0
    ready: float = 30.0
    stop: float = 20.0


def run_launchctl(args: tuple[str, ...], timeout: float, check_cancel: Callable[[], None]) -> str:
    return controller.run_command(('/bin/launchctl', *args), timeout, check_cancel)


def process_start_time(pid: int) -> int:
    # Reuse TProcessUID's Darwin milliseconds exactly; ps timestamps and float
    # create_time conversions do not preserve the readiness record's identity.
    from devtools.local_cache.psingleton.python import systemptr

    return systemptr.get_process_start_time(pid)


def render_plist(launch: configuration.ServiceLaunch, *, label: str, root: str, stop_timeout: float) -> bytes:
    if not all(os.path.isabs(path) for path in (launch.executable, launch.configuration, root)):
        raise ValueError('LaunchAgent paths must be absolute')
    return plistlib.dumps(
        {
            'Label': label,
            'ProgramArguments': list(launch.argv),
            'RunAtLoad': True,
            'KeepAlive': {'SuccessfulExit': False},
            'ThrottleInterval': 10,
            'ExitTimeOut': math.ceil(stop_timeout),
            'AbandonProcessGroup': False,
            'Umask': 0o077,
            'StandardOutPath': os.path.join(root, 'launchd.stdout.log'),
            'StandardErrorPath': os.path.join(root, 'launchd.stderr.log'),
        },
        fmt=plistlib.FMT_XML,
    )


class LaunchdServiceManager:
    """Use launchd for lifecycle, retaining policy in the recovery transaction.

    list's documented table is used instead of parsing print's explicitly
    unstable diagnostic format. Legacy list targets the caller's bootstrap
    domain: require the matching Aqua session before using it with gui/<uid>.
    SSH/background sessions are deliberately unsupported, pending qualification.
    """

    def __init__(
        self,
        paths,
        *,
        label: str = 'ru.yandex.ya.larry',
        timeouts: ManagerTimeouts = ManagerTimeouts(),
        command_runner: Callable = run_launchctl,
        identity_reader: Callable[[int], int] = process_start_time,
        check_cancel: Callable[[], None] = release._check_cancel,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
        uid: int | None = None,
    ) -> None:
        if any(not math.isfinite(value) or value <= 0 for value in timeouts) or timeouts.command <= timeouts.stop:
            raise ValueError('Timeouts must be positive and finite; command timeout must exceed stop timeout')
        self.paths = paths
        self.domain = f'gui/{os.getuid() if uid is None else uid}'
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', label):
            raise ValueError('Expected a plain launchd service label')
        self.label = label
        self.target = f'{self.domain}/{self.label}'
        self.plist_path = os.path.join(os.path.realpath(paths.root), self.label + '.plist')
        self._timeouts = timeouts
        self._run = command_runner
        self._identity = identity_reader
        self._check_cancel = check_cancel
        self._clock = clock
        self._sleep = sleep

    def _command(self, *args, timeout=None):
        self._check_cancel()
        return self._run(tuple(args), self._timeouts.command if timeout is None else timeout, self._check_cancel)

    def _status(self, deadline):
        def command(*args):
            remaining = deadline - self._clock()
            if remaining <= 0:
                raise _StatusDeadlineError('launchd status timed out')
            return self._command(*args, timeout=min(remaining, self._timeouts.command))

        if command('managername').strip() != 'Aqua' or command('manageruid').strip() != self.domain.split('/')[1]:
            raise manager.ServiceManagerError('Larry requires a terminal in the current macOS GUI login session')
        # A failed controller call is never interpreted as an absent service.
        lines = command('list').splitlines()
        if not lines or lines[0].split() != ['PID', 'Status', 'Label']:
            raise manager.ServiceManagerError('Invalid launchctl list output')
        status = manager.ServiceStatus('not-found', 'inactive', 'dead', 0, '')
        for line in lines[1:]:
            # Other LaunchAgents may have whitespace inside their labels.
            fields = line.split(maxsplit=2)
            if len(fields) != 3:
                raise manager.ServiceManagerError('Invalid launchctl service row')
            if fields[2] != self.label:
                continue
            try:
                pid = 0 if fields[0] == '-' else int(fields[0])
                int(fields[1])
                if pid < 0:
                    raise ValueError()
            except ValueError:
                raise manager.ServiceManagerError('Invalid launchctl process status') from None
            identity = self._identity(pid) if pid else 0
            live = bool(pid and identity)
            status = manager.ServiceStatus(
                'loaded',
                'active' if live else 'inactive',
                'running' if live else 'dead',
                pid,
                str(identity) if live else '',
            )
        return status

    def status(self) -> manager.ServiceStatus:
        return self._status(self._clock() + self._timeouts.command)

    def ensure_installed(self, launch: configuration.ServiceLaunch) -> None:
        contents = render_plist(
            launch, label=self.label, root=os.path.realpath(self.paths.root), stop_timeout=self._timeouts.stop
        )
        with installation._locked(self.paths, self._check_cancel):
            try:
                with open(self.plist_path, 'rb') as source:
                    if source.read() == contents:
                        return
            except FileNotFoundError:
                pass
            # Registration is deferred to start/restart: replacing the plist
            # does not update a loaded job and must not stop active work here.
            state_files.write_private_file(self.plist_path, contents, check_cancel=self._check_cancel)

    def _pending_stop(self):
        try:
            with open(os.path.join(self.paths.root, 'launchd-pending-stop.json'), 'rb') as source:
                record = json.loads(source.read(1024))
            if (
                not isinstance(record, dict)
                or type(record.get('pid')) is not int
                or record['pid'] <= 0
                or type(record.get('start')) is not int
                or record['start'] <= 0
            ):
                raise ValueError()
            return record
        except FileNotFoundError:
            return None
        except (ValueError, UnicodeError):
            raise manager.ServiceManagerError(
                'Invalid launchd pending-stop record; inspect the service directory'
            ) from None

    def _wait_stopped(self):
        record = self._pending_stop()
        if record is None:
            return
        deadline = self._clock() + self._timeouts.ready
        while self._identity(record['pid']) == record['start']:
            self._check_cancel()
            if self._clock() >= deadline:
                raise manager.ServiceManagerError('Old Larry is still stopping; no replacement started')
            self._sleep(0.1)
        self._check_cancel()
        os.unlink(os.path.join(self.paths.root, 'launchd-pending-stop.json'))

    def _replace_job(self):
        status = self.status()
        if status.load_state == 'loaded':
            # Persist before bootout so cancellation/client death cannot forget
            # an old process after its job has disappeared from launchd's list.
            with installation._locked(self.paths, self._check_cancel):
                pending = self._pending_stop()
                if pending is not None and self._identity(pending['pid']) == pending['start']:
                    if (pending['pid'], str(pending['start'])) != (status.pid, status.invocation_id):
                        raise manager.ServiceManagerError('Another Larry process is still stopping')
                if status.pid and status.invocation_id:
                    state_files.write_private_file(
                        os.path.join(self.paths.root, 'launchd-pending-stop.json'),
                        json.dumps({'pid': status.pid, 'start': int(status.invocation_id)}).encode(),
                        check_cancel=self._check_cancel,
                    )
            self._command('bootout', self.target)
            deadline = self._clock() + self._timeouts.ready
            while self._status(deadline).load_state != 'not-found':
                self._check_cancel()
                self._sleep(0.1)
        self._wait_stopped()
        self._check_cancel()
        self._command('bootstrap', self.domain, self.plist_path)

    def start(self) -> None:
        # An inactive loaded job may still retain an older ProgramArguments.
        self._replace_job()

    def restart(self) -> None:
        # Do not use kickstart -k: bootout allows Larry's SIGTERM cleanup and
        # bootstrap reloads the selected launch description. Hard kill does not
        # guarantee cleanup of Larry's independent command process groups.
        self._replace_job()

    def wait_ready(self, expected: manager.ReadinessExpectation) -> manager.ReadyInstance:
        deadline = self._clock() + self._timeouts.ready

        def ready_status():
            try:
                return self._status(deadline)
            except _StatusDeadlineError:
                raise manager.ServiceNotReadyError(f'Readiness timed out for {self.target}') from None

        while self._clock() < deadline:
            self._check_cancel()
            status = ready_status()
            if status.load_state != 'loaded':
                raise manager.ServiceNotReadyError(f'LaunchAgent {self.target} is not registered')
            if status.active_state == 'active':
                record = manager.read_ready_instance(self.paths, status, expected)
                if record and str(record.process_start_time) == status.invocation_id:
                    if ready_status() == status and self._identity(status.pid) == record.process_start_time:
                        self._check_cancel()
                        if self._clock() < deadline:
                            return record
            self._sleep(min(0.1, max(0, deadline - self._clock())))
        raise manager.ServiceNotReadyError(f'Readiness timed out for {self.target}; inspect {self.paths.root} logs')
