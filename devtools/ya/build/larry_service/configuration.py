"""Persistent launch inputs; OS-manager orchestration is a separate step."""

import json
import os
import tomllib
import typing
from typing import Callable

from devtools.ya.build.larry_service import installation, paths as service_paths, release, state_files


class ServiceOptions(typing.NamedTuple):
    endpoint: str
    local_cache_directory: str
    yt_proxy_role: str | None = None
    grpc_endpoint: str | None = None


class ServiceLaunch(typing.NamedTuple):
    executable: str
    configuration: str

    @property
    def argv(self) -> tuple[str, ...]:
        return self.executable, '--config', self.configuration


def write_configuration(
    paths: service_paths.ServicePaths,
    artifact_id: str,
    options: ServiceOptions,
    *,
    check_cancel: Callable[[], None] = release._check_cancel,
) -> ServiceLaunch:
    """Bind a launch description to one installation, never to mutable current.

    A shared config would let preparation of another candidate change the
    startup identity of this executable. Keep config outside the hashed artifact
    and let the future manager coordinate exact launch inputs with selection.
    """
    if not options.endpoint or not isinstance(options.endpoint, str):
        raise ValueError('Service endpoint must be nonempty text')
    if not os.path.isabs(options.local_cache_directory):
        raise ValueError('Local cache directory must be absolute')
    if options.yt_proxy_role is not None and not isinstance(options.yt_proxy_role, str):
        raise ValueError('YT proxy role must be text')
    if options.grpc_endpoint is not None and (not isinstance(options.grpc_endpoint, str) or not options.grpc_endpoint):
        raise ValueError('gRPC endpoint must be nonempty text')
    if options.grpc_endpoint == options.endpoint:
        raise ValueError('Service endpoints must be distinct')
    with installation._locked(paths, check_cancel):
        installed = installation._load(paths, artifact_id)
        values = {
            'listen-socket': options.endpoint,
            'local-cache-dir': os.path.realpath(options.local_cache_directory),
            'service-state-file': paths.state_file,
            'installed-artifact-id': installed.artifact_id,
            'yt-token-path': paths.token_file,
        }
        if options.grpc_endpoint is not None:
            values['grpc-endpoint'] = options.grpc_endpoint
        if options.yt_proxy_role is not None:
            values['yt-proxy-role'] = options.yt_proxy_role
        # All fields are strings; JSON's generated string escapes are valid TOML.
        contents = ''.join(f'{key} = {json.dumps(value, ensure_ascii=False)}\n' for key, value in values.items())
        config_path = os.path.join(installed.root, 'service.toml')
        state_files.write_private_file(config_path, contents.encode('utf-8'), check_cancel=check_cancel)
        return ServiceLaunch(installed.executable, config_path)


def configured_endpoint(
    paths: service_paths.ServicePaths,
    artifact_id: str,
    *,
    protocol: str = "legacy",
    check_cancel: Callable[[], None] = release._check_cancel,
) -> str | None:
    """Keep a selected installation's transport binding across client generations."""
    with installation._locked(paths, check_cancel):
        installed = installation._load(paths, artifact_id)
        try:
            with open(os.path.join(installed.root, 'service.toml'), 'rb') as source:
                endpoint = tomllib.load(source).get('grpc-endpoint' if protocol == 'grpc' else 'listen-socket')
        except FileNotFoundError:
            return None
        if protocol == 'grpc' and endpoint is None:
            return None
        if not isinstance(endpoint, str) or not endpoint:
            raise ValueError('Installed service configuration has no endpoint')
        return endpoint
