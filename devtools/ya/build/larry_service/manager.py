"""Describe OS-owned service lifecycle independently of the build transport."""

import json
import sys
import typing

from devtools.ya.build.larry_service import configuration


class ServiceManagerError(RuntimeError):
    pass


class ServiceNotReadyError(ServiceManagerError):
    """The managed process did not become ready; lifecycle recovery is still possible."""


class ServiceStatus(typing.NamedTuple):
    load_state: str
    active_state: str
    sub_state: str
    pid: int
    invocation_id: str


class ReadyInstance(typing.NamedTuple):
    run_id: str
    pid: int
    process_start_time: int
    artifact_id: str
    endpoint: str
    invocation_id: str


class ReadinessExpectation(typing.NamedTuple):
    artifact_id: str | None
    endpoint: str
    previous_run_id: str | None = None


class ServiceManager(typing.Protocol):
    def ensure_installed(self, launch: configuration.ServiceLaunch) -> None: ...
    def status(self) -> ServiceStatus: ...
    def start(self) -> None: ...
    def restart(self) -> None: ...
    def wait_ready(self, expected: ReadinessExpectation) -> ReadyInstance: ...


def read_ready_instance(paths, status, expected):
    try:
        with open(paths.state_file, 'rb') as source:
            data = source.read(64 * 1024 + 1)
        if len(data) > 64 * 1024:
            return None
        record = json.loads(data)
        if not isinstance(record, dict):
            return None
        endpoint = record.get('endpoint')
        if expected.endpoint.startswith('grpc:'):
            grpc_endpoint = record.get('grpc_endpoint')
            endpoint = 'grpc:' + grpc_endpoint if isinstance(grpc_endpoint, str) and grpc_endpoint else None
        if (
            type(record.get('format_version')) is not int
            or record['format_version'] != 1
            or record.get('ready') is not True
            or type(record.get('pid')) is not int
            or record['pid'] != status.pid
            or type(record.get('process_start_time')) is not int
            or record['process_start_time'] <= 0
            or not isinstance(record.get('run_id'), str)
            or not record['run_id']
            or record['run_id'] == expected.previous_run_id
            or not isinstance(record.get('artifact_id'), str)
            or not record['artifact_id']
            or (expected.artifact_id is not None and record['artifact_id'] != expected.artifact_id)
            or endpoint != expected.endpoint
        ):
            return None
        return ReadyInstance(
            record['run_id'],
            status.pid,
            record['process_start_time'],
            record['artifact_id'],
            expected.endpoint,
            status.invocation_id,
        )
    except (FileNotFoundError, ValueError, UnicodeError, RecursionError):
        return None


def create_service_manager(paths, *, check_cancel):
    if sys.platform.startswith('linux'):
        from devtools.ya.build.larry_service import systemd

        return systemd.SystemdServiceManager(paths, check_cancel=check_cancel)
    if sys.platform == 'darwin':
        from devtools.ya.build.larry_service import launchd

        return launchd.LaunchdServiceManager(paths, check_cancel=check_cancel)
    raise ServiceManagerError('Managed Larry requires Linux user systemd or macOS launchd')
