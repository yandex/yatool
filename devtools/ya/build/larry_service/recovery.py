"""Recover the explicitly managed endpoint after a safe admission failure."""

import datetime
import json
import logging
import os
from typing import Callable

import devtools.ya_make.libs.larry_client.py as protocol
from devtools.ya.build.larry_service import (
    address,
    client,
    compatibility,
    configuration,
    installation,
    manager,
    paths as service_paths,
    release,
)

logger = logging.getLogger(__name__)
RELEASE_WINDOW = datetime.timedelta(days=540)


class RecoveryError(protocol.ClientError):
    pass


class ServiceRecovery:
    def _launch_options(self, endpoint, artifact_id, previous_artifact_id=None):
        legacy = configuration.configured_endpoint(self.paths, artifact_id, check_cancel=self._check_cancel)
        grpc = configuration.configured_endpoint(
            self.paths, artifact_id, protocol='grpc', check_cancel=self._check_cancel
        )
        if previous_artifact_id is not None and previous_artifact_id != artifact_id:
            legacy = legacy or configuration.configured_endpoint(
                self.paths, previous_artifact_id, check_cancel=self._check_cancel
            )
            grpc = grpc or configuration.configured_endpoint(
                self.paths, previous_artifact_id, protocol='grpc', check_cancel=self._check_cancel
            )
        if endpoint.startswith('grpc:'):
            grpc = endpoint[5:]
            legacy = legacy or address.resolve_address('local:').address
        else:
            legacy = endpoint
        return configuration.ServiceOptions(legacy, os.path.join(self.paths.root, 'cache'), grpc_endpoint=grpc)

    def __init__(
        self,
        paths: service_paths.ServicePaths,
        *,
        client_date: Callable[[], datetime.date | None],
        service_manager: manager.ServiceManager | None = None,
        prepare_release: Callable[..., release.PreparedRelease] = release.prepare_release,
        metadata_reader: Callable[..., release.ReleaseMetadata] = release.read_release_metadata,
        origin_reader: Callable[..., compatibility.ReleaseOrigin] = release._source_origin,
        check_cancel: Callable[[], None] = release._check_cancel,
    ) -> None:
        self.paths = paths
        self._client_date = client_date
        self._manager = service_manager
        self._prepare = prepare_release
        self._metadata_reader = metadata_reader
        self._origin_reader = origin_reader
        self._check_cancel = check_cancel

    def observe(self) -> str | None:
        # A cheap pre-attempt observation closes the delayed-waiter race. This
        # file is not readiness proof: only the manager can verify a live run.
        try:
            with open(self.paths.state_file, 'rb') as source:
                record = json.loads(source.read(64 * 1024 + 1))
            if isinstance(record, dict) and isinstance(record.get('run_id'), str):
                return record['run_id']
        except (OSError, ValueError, UnicodeError, RecursionError):
            pass
        return None

    def _check_age(self, metadata, origin, client_date):
        outcome = compatibility.check_release_dates(
            compatibility.ReleaseDate(client_date, compatibility.ReleaseOrigin.UNKNOWN),
            compatibility.ReleaseDate(metadata.release_date, origin),
            window=RELEASE_WINDOW,
        )
        logger.info(
            'Larry recovery dates: client=%s service=%s origin=%s age_check=%s',
            client_date,
            metadata.release_date,
            origin.value,
            outcome.value,
        )
        if outcome == compatibility.AgeCheck.OUTSIDE_WINDOW:
            raise RecoveryError(
                f'Update ya: client build date {client_date} is more than 540 days behind '
                f'Larry release date {metadata.release_date}'
            )

    def ensure_service(self, request: client.RecoveryRequest) -> None:
        stage = 'checking platform'
        try:
            if self._manager is None:
                self._manager = manager.create_service_manager(self.paths, check_cancel=self._check_cancel)
            service = self._manager
            stage = 'waiting for recovery lock'
            # Lifecycle needs a wider critical section than individual atomic
            # installation writes. Do not hold installation.lock across calls
            # which acquire it themselves, or across another client's warm RPC.
            with installation._locked(self.paths, self._check_cancel, name='recovery.lock'):
                stage = 'checking installed release dates'
                client_date = self._client_date()
                selected = installation.selected_installation(self.paths, check_cancel=self._check_cancel)
                if selected is not None:
                    self._check_age(selected.metadata, selected.origin, client_date)
                configured_endpoint = (
                    configuration.configured_endpoint(
                        self.paths,
                        selected.artifact_id,
                        protocol='grpc' if request.endpoint.address.startswith('grpc:') else 'legacy',
                        check_cancel=self._check_cancel,
                    )
                    if selected is not None
                    else None
                )
                if configured_endpoint is not None and request.endpoint.address.startswith('grpc:'):
                    configured_endpoint = 'grpc:' + configured_endpoint
                # A missing versioned socket can mean a healthy service for a
                # different transport. Never rebind that release and restart it
                # merely because another generation of ya cannot connect.
                endpoint_changed = configured_endpoint is not None and configured_endpoint != request.endpoint.address
                if endpoint_changed:
                    installed_transport = address.managed_transport_version(configured_endpoint)
                    requested_transport = address.managed_transport_version(request.endpoint.address)
                    if (
                        installed_transport is not None
                        and requested_transport is not None
                        and installed_transport > requested_transport
                    ):
                        raise RecoveryError('Update ya: the managed Larry service uses a newer transport generation')
                stage = 'checking current service'
                status = service.status()
                running = None
                active = status.active_state in ('active', 'activating', 'reloading')
                if active and not endpoint_changed:
                    try:
                        running = service.wait_ready(manager.ReadinessExpectation(None, request.endpoint.address))
                    except manager.ServiceNotReadyError:
                        # Older or unhealthy processes may have no valid readiness
                        # record. They cannot be reused, but the OS manager can
                        # still restart them. Controller failures must propagate.
                        self._check_cancel()
                        logger.info('Current Larry process is not ready; continuing lifecycle recovery')
                if running is not None:
                    with installation._locked(self.paths, self._check_cancel):
                        running_installation = installation._load(self.paths, running.artifact_id)
                    self._check_age(running_installation.metadata, running_installation.origin, client_date)
                    if running.run_id != request.observed_run_id:
                        logger.info('Reusing recovered Larry run=%s artifact=%s', running.run_id, running.artifact_id)
                        return
                rejected = endpoint_changed or isinstance(request.first_attempt_error, protocol.RemoteError)
                if selected is None or (rejected and (running is None or running.artifact_id == selected.artifact_id)):
                    if selected is not None and selected.origin != compatibility.ReleaseOrigin.OFFICIAL:
                        stage = 'qualifying installed release'
                        origin = self._origin_reader(selected.resource_uri, check_cancel=self._check_cancel)
                        self._check_age(selected.metadata, origin, client_date)
                        selected = installation.confirm_installation_origin(
                            self.paths, selected.artifact_id, origin, check_cancel=self._check_cancel
                        )
                    stage = 'preparing release'
                    candidate = self._prepare(check_cancel=self._check_cancel)
                    self._check_age(candidate.metadata, candidate.origin, client_date)
                    if selected is not None and (selected.resource_uri, selected.metadata) == (
                        candidate.resource_uri,
                        candidate.metadata,
                    ):
                        raise RecoveryError(
                            'Tool delivery selected the same rejected Larry release; no update available'
                        )
                    stage = 'installing release'
                    installed = installation.prepare_installation(
                        self.paths,
                        candidate,
                        origin=candidate.origin,
                        metadata_reader=self._metadata_reader,
                        check_cancel=self._check_cancel,
                    )
                    # Persist the transport binding before publishing current:
                    # another client must not bind this artifact to its older
                    # endpoint if we exit immediately after activation.
                    stage = 'configuring prepared release'
                    configuration.write_configuration(
                        self.paths,
                        installed.artifact_id,
                        self._launch_options(
                            request.endpoint.address,
                            installed.artifact_id,
                            selected.artifact_id if selected is not None else None,
                        ),
                        check_cancel=self._check_cancel,
                    )
                    stage = 'activating release'
                    activated = installation.activate_installation(
                        self.paths, installed.artifact_id, check_cancel=self._check_cancel
                    )
                    if activated.decision == installation.ActivationDecision.NOT_NEWER:
                        raise RecoveryError(
                            'Larry replacement requires confirmed official origins and a strictly newer release date; '
                            f'installed={activated.selected.metadata.release_date} candidate={candidate.metadata.release_date}'
                        )
                    selected = activated.selected
                stage = 'writing launch configuration'
                launch = configuration.write_configuration(
                    self.paths,
                    selected.artifact_id,
                    self._launch_options(request.endpoint.address, selected.artifact_id),
                    check_cancel=self._check_cancel,
                )
                service.ensure_installed(launch)
                self._check_cancel()
                logger.info(
                    'Starting Larry recovery: artifact=%s previous_run=%s',
                    selected.artifact_id,
                    running.run_id if running else None,
                )
                if not active:
                    stage = 'starting service'
                    service.start()
                else:
                    stage = 'restarting service (no rollback)'
                    service.restart()
                stage = 'waiting for readiness (no rollback)'
                ready = service.wait_ready(
                    manager.ReadinessExpectation(
                        selected.artifact_id, request.endpoint.address, running.run_id if running else None
                    )
                )
                self._check_cancel()
                logger.info('Larry recovery completed: run=%s artifact=%s', ready.run_id, ready.artifact_id)
        except Exception as error:
            self._check_cancel()
            if isinstance(error, RecoveryError):
                error.add_note(f'Larry recovery stage: {stage}')
                raise
            # Tool errors can contain transport credentials. Keep the cause for
            # adapter redaction, but expose only the stage and error family.
            detail = (
                str(error)
                if isinstance(
                    error,
                    (
                        manager.ServiceManagerError,
                        installation.InstallationError,
                        release.ReleasePreparationError,
                        configuration.ConfigurationError,
                    ),
                )
                else type(error).__name__
            )
            raise RecoveryError(f'Larry recovery failed while {stage}: {detail}') from error
