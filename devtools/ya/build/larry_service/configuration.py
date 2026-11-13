"""Persistent launch inputs; OS-manager orchestration is a separate step."""

import json
import os
import string
import tomllib
import typing
from typing import Callable

from devtools.ya.build.larry_service import installation, paths as service_paths, release, state_files
import library.python.resource as resource


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


class ConfigurationError(ValueError):
    pass


def render_configuration(
    template: str,
    paths: service_paths.ServicePaths,
    artifact_id: str,
    options: ServiceOptions,
) -> str:
    """Render startup inputs and verify the managed lifecycle binding."""
    if not isinstance(options.endpoint, str) or not options.endpoint:
        raise ConfigurationError('Service endpoint must be nonempty text')
    if not os.path.isabs(options.local_cache_directory):
        raise ConfigurationError('Local cache directory must be absolute')
    if options.yt_proxy_role is not None and not isinstance(options.yt_proxy_role, str):
        raise ConfigurationError('YT proxy role must be text')
    if options.grpc_endpoint is not None and (not isinstance(options.grpc_endpoint, str) or not options.grpc_endpoint):
        raise ConfigurationError('gRPC endpoint must be nonempty text')
    if options.grpc_endpoint == options.endpoint:
        raise ConfigurationError('Service endpoints must be distinct')
    values = {
        'endpoint': options.endpoint,
        'local_cache_directory': os.path.realpath(options.local_cache_directory),
        'service_state_file': paths.state_file,
        'installed_artifact_id': artifact_id,
        'yt_token_path': paths.token_file,
    }
    # Substitute complete TOML string literals, never raw text inside quotes.
    # Escape all control characters (including DEL) accepted in filesystem paths.
    slots = {key: json.dumps(value, ensure_ascii=False).replace('\x7f', '\\u007f') for key, value in values.items()}
    # TOML has no null. An optional assignment preserves absent versus empty role.
    slots['yt_proxy_role_setting'] = ''
    if options.yt_proxy_role is not None:
        role = json.dumps(options.yt_proxy_role, ensure_ascii=False).replace('\x7f', '\\u007f')
        slots['yt_proxy_role_setting'] = f'yt-proxy-role = {role}'
    slots['grpc_endpoint_setting'] = ''
    if options.grpc_endpoint is not None:
        endpoint = json.dumps(options.grpc_endpoint, ensure_ascii=False).replace('\x7f', '\\u007f')
        slots['grpc_endpoint_setting'] = f'grpc-endpoint = {endpoint}'
    # Unlike safe_substitute or a full expression engine, Template rejects unknown
    # slots without evaluating release-provided code. Unused inputs are allowed so
    # a release template can omit settings it does not support.
    try:
        contents = string.Template(template).substitute(slots)
    except KeyError as error:
        raise ConfigurationError(f'Unknown Larry configuration slot: {error.args[0]}') from None
    except ValueError:
        raise ConfigurationError('Invalid Larry configuration template syntax') from None
    try:
        parsed = tomllib.loads(contents)
    except tomllib.TOMLDecodeError:
        raise ConfigurationError('Rendered Larry configuration is not valid TOML') from None
    # Recovery still reads listen-socket by name. Keep that dependency explicit
    # until endpoint discovery is separated from the service configuration format.
    for key, expected in (
        ('listen-socket', options.endpoint),
        ('service-state-file', paths.state_file),
        ('installed-artifact-id', artifact_id),
    ):
        if parsed.get(key) != expected:
            raise ConfigurationError(f'Larry configuration must preserve {key}')
    if options.grpc_endpoint is not None and parsed.get('grpc-endpoint') != options.grpc_endpoint:
        raise ConfigurationError('Larry configuration must preserve grpc-endpoint')
    return contents


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
    with installation._locked(paths, check_cancel):
        installed = installation._load(paths, artifact_id)
        template = resource.find('/larry_service/service.toml.template').decode('utf-8')
        contents = render_configuration(template, paths, installed.artifact_id, options)
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
