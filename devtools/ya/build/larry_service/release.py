"""Prepare an observed tool-cache candidate; installation is a separate step."""

import datetime
import json
import logging
import math
import os
import subprocess
import time
from typing import Callable, NamedTuple

from devtools.ya.build.larry_service import compatibility
from exts.process import popen
from yalibrary import tools
from yalibrary.fetcher.cache_helper import safe_resource_lock
from yalibrary.fetcher.common import get_sandbox_token

_MAX_RELEASE_METADATA_SIZE = 16 * 1024
_MAX_RESOURCE_URI_SIZE = 4096


class ReleaseMetadata(NamedTuple):
    release_id: str | None
    release_date: datetime.date | None


class PreparedRelease(NamedTuple):
    root: str
    executable: str
    resource_uri: str
    metadata: ReleaseMetadata
    origin: compatibility.ReleaseOrigin = compatibility.ReleaseOrigin.UNKNOWN


class ReleasePreparationError(RuntimeError):
    pass


class ReleaseMetadataError(ReleasePreparationError):
    pass


def _check_cancel() -> None:
    try:
        import app_ctx
    except ImportError:
        return
    state = getattr(app_ctx, 'state', None)
    if state is not None:
        state.check_cancel_state()


def _unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ReleaseMetadataError('Duplicate release metadata field')
        result[key] = value
    return result


def parse_release_metadata(output: bytes) -> ReleaseMetadata:
    if len(output) > _MAX_RELEASE_METADATA_SIZE:
        raise ReleaseMetadataError('Release metadata exceeds the output limit')
    try:
        text = output.decode('utf-8')
        if len(text.strip().splitlines()) != 1:
            raise ValueError()
        value = json.loads(text, object_pairs_hook=_unique_fields)
        if not isinstance(value, dict) or type(value.get('format_version')) is not int or value['format_version'] != 1:
            raise ValueError()
        release_id = value['release_id']
        if release_id is not None and (not isinstance(release_id, str) or not release_id):
            raise ValueError()
        date = value['release_date']
        if date is not None:
            if not isinstance(date, str):
                raise ValueError()
            parsed_date = datetime.date.fromisoformat(date)
            if parsed_date.isoformat() != date:
                raise ValueError()
            date = parsed_date
        return ReleaseMetadata(release_id, date)
    except (ValueError, KeyError, TypeError, UnicodeError, RecursionError):
        raise ReleaseMetadataError('Invalid Larry release metadata') from None


def read_release_metadata(
    executable: str, *, timeout: float = 10.0, check_cancel: Callable[[], None] = _check_cancel
) -> ReleaseMetadata:
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('Version timeout must be positive')
    check_cancel()
    process = popen(
        [executable, '--version'],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )
    with process.stdout:
        try:
            # Never wait for EOF on an inherited writer owned by a descendant.
            # Raw nonblocking pipe reads keep collection and cleanup bounded.
            os.set_blocking(process.stdout.fileno(), False)
            deadline = time.monotonic() + timeout
            data = bytearray()
            while True:
                check_cancel()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise ReleaseMetadataError('Larry version probe timed out')
                chunk = process.stdout.read(_MAX_RELEASE_METADATA_SIZE + 1 - len(data))
                if chunk:
                    data.extend(chunk)
                    if len(data) > _MAX_RELEASE_METADATA_SIZE:
                        raise ReleaseMetadataError('Release metadata exceeds the output limit')
                    continue
                if chunk == b'' and process.poll() is not None:
                    break
                time.sleep(min(0.1, remaining))
            check_cancel()
            if process.returncode:
                raise ReleaseMetadataError('Larry version probe failed')
            return parse_release_metadata(bytes(data))
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()


def _read_resource_uri(root: str) -> str:
    try:
        with open(os.path.join(root, 'lnk'), 'rb') as manifest:
            resource_uri = manifest.read(_MAX_RESOURCE_URI_SIZE + 1)
        if len(resource_uri) > _MAX_RESOURCE_URI_SIZE:
            raise ValueError()
        resource_uri = resource_uri.decode('utf-8')
        if not resource_uri or any(char.isspace() for char in resource_uri):
            raise ValueError()
        return resource_uri
    except (OSError, ValueError):
        raise ReleasePreparationError('Invalid Larry resource manifest') from None


def prepare_release(
    *,
    tool_resolver=tools.tool,
    origin_reader: Callable[..., compatibility.ReleaseOrigin] | None = None,
    metadata_reader=read_release_metadata,
    check_cancel: Callable[[], None] = _check_cancel,
) -> PreparedRelease:
    """Prepare through ordinary tool delivery; returned cache paths are borrowed."""
    check_cancel()
    executable, params = tool_resolver('larry-betta', with_params=True)
    check_cancel()
    # Tool delivery updates its platform alias under this same lock. Capture the
    # actual resource and its existing deployment manifest before probing it.
    with safe_resource_lock(params['toolchain_root_path']):
        root = os.path.realpath(params['toolchain_root_path'])
        executable = os.path.realpath(executable)
        if not os.path.isfile(executable):
            raise ReleasePreparationError('Prepared Larry executable is missing')
        resource_uri = _read_resource_uri(root)
        metadata = metadata_reader(executable, check_cancel=check_cancel)
        check_cancel()
        origin = (origin_reader or _source_origin)(resource_uri, check_cancel=check_cancel)
        return PreparedRelease(root, executable, resource_uri, metadata, origin)


def _source_origin(resource_uri: str, *, check_cancel: Callable[[], None]) -> compatibility.ReleaseOrigin:
    # The latest alias cache is not keyed by its query: after a config override
    # it may still name a testing resource. Qualify the actual ID, not today's
    # tool definition, using the existing Sandbox metadata client.
    prefix, _, resource_id = resource_uri.partition(':')
    if prefix != 'sbr' or not resource_id.isdecimal():
        return compatibility.ReleaseOrigin.UNKNOWN
    check_cancel()
    try:
        from devtools.ya.yalibrary.yandex.sandbox import SandboxClient

        sandbox = SandboxClient(
            token=get_sandbox_token(),
            rest_params={
                'total_wait': 10,
                'min_timeout': 5,
                'max_timeout': 5,
                'check_for_cancel': check_cancel,
            },
        )
        resources = sandbox.list_resources_no_links(
            id=int(resource_id),
            type='LARRY',
            owner='YATOOL',
            releases='prestable',
            limit=1,
        )
        check_cancel()
        if any(str(item.get('id')) == resource_id for item in resources):
            return compatibility.ReleaseOrigin.OFFICIAL
    except Exception:
        check_cancel()
        # An unavailable metadata service is not evidence of beta-channel origin.
        # Never include server responses or authentication details in logs.
        logging.getLogger(__name__).warning('Cannot confirm Larry resource provenance for %s', resource_uri)
    return compatibility.ReleaseOrigin.UNKNOWN
