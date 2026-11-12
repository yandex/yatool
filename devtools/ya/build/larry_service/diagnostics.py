"""Bounded Larry diagnostics persisted through ya's existing debug bundle."""

import contextlib
import json
import logging
import os
import re
import stat
import sys
import time
from collections.abc import Callable, Iterator, MutableMapping, Sequence
from typing import Any

from devtools.ya.core import sec

MAX_LOG_BYTES = 64 * 1024
MAX_FIELD_BYTES = 4096
_STATE_FIELDS = ('run_id', 'artifact_id', 'pid', 'process_start_time', 'ready', 'endpoint')
_UUID = r'[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}'
_SESSION_PATH = re.compile(
    rf'/(?:[^/]+/)*sessions/(?P<request_id>{_UUID})/' rf'(?:stderr/stderr-[0-9]+\.log|results/{_UUID}/[0-9]+)'
)
_LOG_PREFIX = re.compile(
    r'\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+\] ' r'\[(?:trace|debug|info|warning|error|critical)\] '
)


def observe_session_path(store: MutableMapping, report: dict[str, Any], path: str | None) -> None:
    """Mine identity only from server-owned callback paths, never user text."""
    # PrepareBuild creates sessions/<request-id>; its stderr store and result
    # publisher hand these paths to the callback for this connection. This is
    # optional evidence from the current layout, not a new wire guarantee.
    if path is None or report.get('request_id_status') == 'conflicting_paths':
        return
    if any(part in ('.', '..') for part in path.split('/')):
        return
    match = _SESSION_PATH.fullmatch(path)
    if match is None:
        return
    observed = match['request_id']
    previous = report.get('request_id')
    if previous == observed:
        return
    if previous is not None:
        report.pop('request_id')
        report['request_id_status'] = 'conflicting_paths'
    else:
        report['request_id'] = observed
        report['request_id_status'] = 'session_path'
    # Retain evidence before ACK/cleanup, including a later callback failure.
    _publish(store, report)


def _request_id(report: dict[str, Any]) -> str | None:
    value = report.get('request_id')
    return value if isinstance(value, str) and re.fullmatch(_UUID, value) else None


def _request_lines(text: str, request_id: str | None) -> str:
    if request_id is None:
        return text
    marker = re.compile(rf'Larry request={request_id}(?=[: \n]|$)')
    lines = []
    for line in text.splitlines(keepends=True):
        prefix = _LOG_PREFIX.search(line)
        if prefix is not None and marker.match(line, prefix.end()):
            lines.append(line)
    # The supplementary view identifies tagged lines within retained context,
    # including journald's short-iso prefix. Untagged continuations remain in
    # that context and are not assigned to a particular request.
    return ''.join(lines)


def _text(value: str, suppressions: Sequence[str], limit: int = MAX_FIELD_BYTES) -> str:
    # Redact before truncation: otherwise a token crossing the boundary can leak.
    return (
        sec.cleanup(value, [secret for secret in suppressions if secret])
        .encode('utf-8')[:limit]
        .decode('utf-8', errors='ignore')
    )


def _log_entry(text: str, request_id: str | None, suppressions: Sequence[str]) -> dict[str, Any]:
    # Shared resource/cache and supervisor events can explain this build's
    # failure. Always retain bounded service context; correlation is navigation
    # within it, never a reason to discard untagged or concurrent-build records.
    context = _text(text, suppressions, MAX_LOG_BYTES)
    entry = {'status': 'ok', 'scope': 'service_context', 'text': context}
    if request_id is not None:
        entry['request_matches'] = {
            'scope': 'request_tagged_lines',
            'coverage': 'within_retained_context',
            'request_id': request_id,
            'text': _request_lines(context, request_id),
        }
    return entry


def _read_file(filename: str, *, tail: bool = False) -> bytes:
    # Only known diagnostic files are read; never follow a link to user data or
    # block on a FIFO accidentally replacing a log. No directory is archived.
    descriptor = os.open(filename, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(descriptor, 'rb') as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError('Diagnostic source is not a regular file')
        if tail:
            source.seek(max(0, info.st_size - MAX_LOG_BYTES))
        data = source.read(MAX_LOG_BYTES + 1)
        # Check before discarding a partial prefix: a concurrent append must
        # not bypass the limit and leave a truncated credential at the end.
        if len(data) > MAX_LOG_BYTES:
            raise ValueError('Diagnostic source exceeds the limit')
        if tail and info.st_size > MAX_LOG_BYTES:
            # Discard a partial first line, including any truncated credential.
            data = data.partition(b'\n')[2]
        return data


def _instance(root: str | None, suppressions: Sequence[str]) -> dict[str, Any]:
    if root is None:
        return {}
    try:
        state = json.loads(_read_file(os.path.join(root, 'service-state.json')))
        result = {
            key: _text(value, suppressions) if isinstance(value, str) else value
            for key in _STATE_FIELDS
            if isinstance(value := state.get(key), (str, int, bool))
        }
        release = state.get('release')
        if isinstance(release, dict):
            result['release'] = {
                key: _text(value, suppressions)
                for key in ('release_id', 'release_date')
                if isinstance(value := release.get(key), str)
            }
        return result
    except (OSError, ValueError, AttributeError, RecursionError):
        return {}


def _publish(store: MutableMapping, report: dict[str, Any]) -> None:
    try:
        # Reassign to emit an evlog snapshot. Retain the same live dict in
        # Store.data so successive snapshots do not trigger overwrite warnings.
        store['larry_diagnostics'] = report
    except Exception:
        logging.getLogger(__name__).debug('Cannot store Larry diagnostics', exc_info=True)


@contextlib.contextmanager
def capture(
    store: MutableMapping,
    address: str,
    *,
    transport_version: int,
    client_release_date: str | None = None,
    service_root: str | None = None,
    suppressions: Sequence[str] = (),
    configuration: dict[str, Any] | None = None,
) -> Iterator[dict[str, Any]]:
    # YMAKE-2419: reuse evlog so diagnostics survive build-root cleanup and do
    # not depend on a live Larry RPC endpoint when the user runs dump debug.
    report = {
        'format_version': 1,
        'address': _text(address, suppressions),
        'transport_version': transport_version,
        'client_release_date': client_release_date,
        'platform': sys.platform,
        'started_at': time.time(),
        'outcome': 'in_progress',
        'request_id_status': 'unavailable',
        'configuration': configuration or {},
        'instance_before': _instance(service_root, suppressions),
    }
    if service_root is not None:
        report['service_root'] = service_root
    _publish(store, report)
    try:
        yield report
    except BaseException as error:
        report['outcome'] = 'interrupted' if not isinstance(error, Exception) else 'error'
        report['error_type'] = type(error).__name__
        report['error'] = _text(str(error), suppressions)
        for field in ('stage', 'reason', 'code'):
            value = getattr(error, field, None)
            if value is not None:
                report['error_' + field] = _text(str(value), suppressions)
        raise
    finally:
        report['finished_at'] = time.time()
        report['instance_after'] = _instance(report.get('service_root'), suppressions)
        _publish(store, report)


def attach_service(
    store: MutableMapping,
    report: dict[str, Any],
    root: str,
    *,
    suppressions: Sequence[str] = (),
) -> None:
    """Attach resolved service paths before credentials or recovery can fail."""
    report['service_root'] = root
    report['instance_before'] = _instance(root, suppressions)
    _publish(store, report)


def _journal(argv: tuple[str, ...]) -> str:
    from devtools.ya.build.larry_service import controller

    return controller.run_command(argv, 5.0, lambda: None)


def collect(
    report: dict[str, Any],
    *,
    suppressions: Sequence[str] = (),
    command_runner: Callable[[tuple[str, ...]], str] = _journal,
) -> dict[str, Any]:
    """Add service logs without contacting, restarting or installing Larry."""
    result = dict(report)
    root = report.get('service_root')
    if root is None:
        result['server_logs'] = {
            'status': 'unavailable',
            'reason': 'Managed service location is unavailable for this invocation',
        }
        return result

    # The credential file is never added to the report or its list of paths.
    # Include the current service token in the existing ya suppression filter.
    secrets = list(suppressions)
    try:
        token = _read_file(os.path.join(root, 'yt-token')).decode('utf-8').strip()
        if token:
            secrets.append(token)
    except (OSError, ValueError):
        pass

    request_id = _request_id(report)
    logs = {}
    if report['platform'].startswith('linux'):
        # Match the recorded invocation's time window, not dump time or the
        # currently running process. Journald remains readable after shutdown.
        until = report.get('finished_at', report['started_at'] + 300)
        argv = (
            'journalctl',
            '--user',
            '--unit=ya-larry.service',
            '--no-pager',
            '--quiet',
            '--output=short-iso',
            '--lines=200',
            '--since=@{:.6f}'.format(report['started_at'] - 5),
            '--until=@{:.6f}'.format(until + 5),
        )
        try:
            logs['journal'] = _log_entry(command_runner(argv), request_id, secrets)
        except Exception as error:
            # Controller errors omit raw subprocess output. Never include an
            # arbitrary exception message from external collectors in the bundle.
            logs['journal'] = {'status': 'unavailable', 'error_type': type(error).__name__}
    elif report['platform'] == 'darwin':
        for name in ('launchd.stdout.log', 'launchd.stderr.log'):
            try:
                data = _read_file(os.path.join(root, name), tail=True).decode('utf-8', errors='replace')
                logs[name] = _log_entry(data, request_id, secrets)
                logs[name]['coverage'] = 'recent_tail'
            except (OSError, ValueError) as error:
                logs[name] = {'status': 'unavailable', 'error_type': type(error).__name__}
    else:
        logs['status'] = 'unsupported_platform'
    result['server_logs'] = logs
    return result
