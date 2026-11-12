"""Independent installations; process lifecycle remains with the OS manager."""

import contextlib
import enum
import hashlib
import json
import os
import re
import shlex
import stat
import time
import uuid
import typing
from typing import Callable

from exts import fs
from library.python import filelock

from devtools.ya.build.larry_service import compatibility, paths as service_paths, release, state_files


class InstallationError(RuntimeError):
    pass


class Installation(typing.NamedTuple):
    artifact_id: str
    root: str
    resource_uri: str
    metadata: release.ReleaseMetadata
    origin: compatibility.ReleaseOrigin
    tree_digest: str

    @property
    def executable(self) -> str:
        return os.path.join(self.root, 'larry')


class ActivationDecision(enum.Enum):
    SELECTED = 'selected'
    ALREADY_SELECTED = 'already_selected'
    NOT_NEWER = 'not_newer'


class ActivationResult(typing.NamedTuple):
    selected: Installation
    decision: ActivationDecision


def _private_directory(path):
    os.makedirs(path, mode=0o700, exist_ok=True)
    info = os.lstat(path)
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077:
        raise InstallationError('Service directory must be private and owned by the current user')


@contextlib.contextmanager
def _locked(paths, check_cancel, *, name="installation.lock"):
    check_cancel()
    if os.name == 'nt':
        raise InstallationError('Managed service installation requires POSIX')
    if not os.path.isabs(paths.root) or os.path.normpath(paths.root) != paths.root:
        raise ValueError('Service root must be an absolute normalized path')
    _private_directory(paths.root)
    # Unlike tool-cache installation, service storage outlives the client.
    # Reuse the OS lock primitive with cancellable waiting, without TC ownership.
    lock = filelock.FileLock(os.path.join(paths.root, name))
    while not lock.acquire(blocking=False):
        check_cancel()
        time.sleep(0.1)
    try:
        check_cancel()
        _private_directory(paths.installations)
        yield
    finally:
        lock.release()


def _relative(path, root):
    if os.path.commonpath([path, root]) != root or path == root:
        raise InstallationError('Artifact path escapes its root')
    return os.path.relpath(path, root)


def _inventory(root, check_cancel):
    """Normalize internal links so source and independent copy are comparable."""
    root = os.path.realpath(root)
    entries = {}

    def walk_error(error):
        raise error

    for directory, dirs, files in os.walk(root, followlinks=False, onerror=walk_error):
        for name in sorted(dirs + files):
            check_cancel()
            path = os.path.join(directory, name)
            relative = os.path.relpath(path, root)
            info = os.lstat(path)
            if stat.S_ISLNK(info.st_mode):
                target = os.path.realpath(path, strict=True)
                if target != root:
                    _relative(target, root)
                entries[relative] = ['link', os.path.relpath(target, directory)]
            elif stat.S_ISDIR(info.st_mode):
                entries[relative] = ['dir', stat.S_IMODE(info.st_mode)]
            elif stat.S_ISREG(info.st_mode):
                digest = hashlib.sha256()
                with open(path, 'rb') as source:
                    while chunk := source.read(1024 * 1024):
                        check_cancel()
                        digest.update(chunk)
                entries[relative] = ['file', stat.S_IMODE(info.st_mode), digest.hexdigest()]
            else:
                raise InstallationError('Unsupported artifact entry')
    return entries


def _digest(inventory):
    return hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()


def _copy_artifact(source, destination, inventory, check_cancel):
    # Generic copy_tree/hardlink_tree cannot verify a borrowed cache snapshot
    # or guarantee that copied symlinks remain inside independent service storage.
    os.mkdir(destination, 0o700)
    # Parents first, with writable modes until all children and links exist.
    for relative, entry in sorted(inventory.items(), key=lambda item: (item[0].count(os.sep), item[0])):
        check_cancel()
        target = os.path.join(destination, relative)
        if entry[0] == 'dir':
            os.mkdir(target, 0o700)
        elif entry[0] == 'link':
            os.symlink(entry[1], target)
        else:
            with open(os.path.join(source, relative), 'rb') as src, open(target, 'xb') as dst:
                while chunk := src.read(1024 * 1024):
                    check_cancel()
                    dst.write(chunk)
            os.chmod(target, entry[1])
    for relative, entry in inventory.items():
        if entry[0] == 'dir':
            os.chmod(os.path.join(destination, relative), entry[1])


def _remove(path):
    if os.path.islink(path):
        os.unlink(path)
    else:
        fs.remove_tree_with_perm_update(path)
        if os.path.lexists(path):
            raise InstallationError('Could not remove obsolete installation')


def _recover_staging(paths):
    # All writers hold the same lock; these names cannot belong to a live writer.
    for name in os.listdir(paths.root):
        if re.fullmatch(r'\.(staging|current)-[0-9a-f]{32}', name):
            _remove(os.path.join(paths.root, name))


def _launcher(executable):
    # Preserve the artifact's own launch path for scripts/resources using argv[0].
    # exec retains the service PID and forwards every argument without a shell layer.
    return '#!/bin/sh\nexec ' + shlex.quote(executable) + ' "$@"\n'


def prepare_installation(
    paths: service_paths.ServicePaths,
    candidate: release.PreparedRelease,
    *,
    origin: compatibility.ReleaseOrigin = compatibility.ReleaseOrigin.UNKNOWN,
    metadata_reader: Callable[..., release.ReleaseMetadata] = release.read_release_metadata,
    check_cancel: Callable[[], None] = release._check_cancel,
) -> Installation:
    """Copy and validate borrowed paths. Origin must come from trusted delivery metadata."""
    if not isinstance(origin, compatibility.ReleaseOrigin):
        raise ValueError('Release origin must be explicit')
    source = os.path.realpath(candidate.root)
    service_root = os.path.realpath(paths.root)
    if os.path.commonpath([source, service_root]) in (source, service_root):
        raise InstallationError('Installation storage must be independent of the source tree')
    executable = _relative(os.path.realpath(candidate.executable), source)
    with _locked(paths, check_cancel):
        _recover_staging(paths)
        artifact_id = uuid.uuid4().hex
        staging = os.path.join(paths.root, '.staging-' + artifact_id)
        os.mkdir(staging, 0o700)
        try:
            inventory = _inventory(source, check_cancel)
            artifact = os.path.join(staging, 'artifact')
            _copy_artifact(source, artifact, inventory, check_cancel)
            if inventory != _inventory(source, check_cancel) or inventory != _inventory(artifact, check_cancel):
                raise InstallationError('Artifact changed during installation')
            if release._read_resource_uri(artifact) != candidate.resource_uri:
                raise InstallationError('Resource identity changed during installation')
            copied_executable = os.path.join(artifact, executable)
            if not os.path.isfile(copied_executable) or not os.access(copied_executable, os.X_OK):
                raise InstallationError('Installed executable is missing or not executable')
            if metadata_reader(copied_executable, check_cancel=check_cancel) != candidate.metadata:
                raise InstallationError('Release metadata changed during installation')
            if inventory != _inventory(artifact, check_cancel):
                raise InstallationError('Artifact changed during metadata inspection')
            target = os.path.join(paths.installations, artifact_id)
            launcher = os.path.join(staging, 'larry')
            with open(launcher, 'x') as output:
                output.write(_launcher(os.path.join(target, 'artifact', executable)))
            os.chmod(launcher, 0o700)
            manifest = dict(
                format_version=1,
                resource_uri=candidate.resource_uri,
                origin=origin.value,
                release_id=candidate.metadata.release_id,
                release_date=candidate.metadata.release_date.isoformat() if candidate.metadata.release_date else None,
                executable=executable,
                tree_digest=_digest(inventory),
            )
            with open(os.path.join(staging, 'installation.json'), 'x') as output:
                json.dump(manifest, output)
            check_cancel()
            os.rename(staging, target)
            return _load(paths, artifact_id)
        finally:
            if os.path.lexists(staging):
                _remove(staging)


def _load(paths, artifact_id):
    if not re.fullmatch('[0-9a-f]{32}', artifact_id):
        raise InstallationError('Invalid installation identity')
    root = os.path.join(paths.installations, artifact_id)
    try:
        if os.path.islink(root):
            raise ValueError()
        with open(os.path.join(root, 'installation.json'), 'rb') as source:
            raw = source.read(64 * 1024 + 1)
        if len(raw) > 64 * 1024:
            raise ValueError()
        data = json.loads(raw)
        metadata = release.parse_release_metadata(
            json.dumps(
                dict(
                    format_version=data['format_version'],
                    release_id=data['release_id'],
                    release_date=data['release_date'],
                )
            ).encode()
        )
        origin = compatibility.ReleaseOrigin(data['origin'])
        resource_uri = data['resource_uri']
        digest = data['tree_digest']
        executable = data['executable']
        if not isinstance(resource_uri, str) or not resource_uri or any(c.isspace() for c in resource_uri):
            raise ValueError()
        if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError()
        if not isinstance(executable, str) or os.path.isabs(executable):
            raise ValueError()
        artifact = os.path.join(root, 'artifact')
        _relative(os.path.realpath(os.path.join(artifact, executable)), artifact)
        launcher = os.path.join(root, 'larry')
        if not stat.S_ISREG(os.lstat(launcher).st_mode) or not os.access(launcher, os.X_OK):
            raise ValueError()
        expected = _launcher(os.path.join(artifact, executable))
        with open(launcher) as source:
            if source.read(len(expected) + 1) != expected:
                raise ValueError()
        return Installation(artifact_id, root, resource_uri, metadata, origin, digest)
    except (OSError, ValueError, TypeError, KeyError):
        raise InstallationError('Invalid installation manifest') from None


def _selected(paths):
    if not os.path.lexists(paths.current):
        return None
    try:
        target = os.readlink(paths.current)
    except OSError:
        raise InstallationError('Invalid selected installation') from None
    artifact_id = os.path.basename(target)
    if target != os.path.join('installations', artifact_id):
        raise InstallationError('Invalid selected installation')
    return _load(paths, artifact_id)


def selected_installation(
    paths: service_paths.ServicePaths, *, check_cancel: Callable[[], None] = release._check_cancel
) -> Installation | None:
    with _locked(paths, check_cancel):
        return _selected(paths)


def confirm_installation_origin(
    paths: service_paths.ServicePaths,
    artifact_id: str,
    origin: compatibility.ReleaseOrigin,
    *,
    check_cancel: Callable[[], None] = release._check_cancel,
) -> Installation:
    """Persist independently verified provenance without recopying the artifact.

    First installation may proceed during a metadata outage. Requalification
    prevents that transient UNKNOWN from permanently disabling future updates.
    The caller must qualify the concrete resource ID through trusted metadata.
    """
    with _locked(paths, check_cancel):
        installed = _load(paths, artifact_id)
        if origin == compatibility.ReleaseOrigin.OFFICIAL and installed.origin != origin:
            path = os.path.join(installed.root, 'installation.json')
            with open(path) as source:
                manifest = json.load(source)
            manifest['origin'] = origin.value
            state_files.write_private_file(path, json.dumps(manifest).encode(), check_cancel=check_cancel)
            return _load(paths, artifact_id)
        return installed


def activate_installation(
    paths: service_paths.ServicePaths, artifact_id: str, *, check_cancel: Callable[[], None] = release._check_cancel
) -> ActivationResult:
    """Select a complete installation; never start or restart the service."""
    with _locked(paths, check_cancel):
        candidate = _load(paths, artifact_id)
        if _digest(_inventory(os.path.join(candidate.root, 'artifact'), check_cancel)) != candidate.tree_digest:
            raise InstallationError('Installed artifact is incomplete or changed')
        current = _selected(paths)
        if current is not None:
            if current.resource_uri == candidate.resource_uri and current.metadata == candidate.metadata:
                return ActivationResult(current, ActivationDecision.ALREADY_SELECTED)
            if not (
                current.origin == candidate.origin == compatibility.ReleaseOrigin.OFFICIAL
                and current.metadata.release_date is not None
                and candidate.metadata.release_date is not None
                and candidate.metadata.release_date > current.metadata.release_date
            ):
                return ActivationResult(current, ActivationDecision.NOT_NEWER)
        temporary = os.path.join(paths.root, '.current-' + uuid.uuid4().hex)
        try:
            os.symlink(os.path.join('installations', artifact_id), temporary)
            check_cancel()
            os.replace(temporary, paths.current)
        finally:
            if os.path.lexists(temporary):
                os.unlink(temporary)
        return ActivationResult(candidate, ActivationDecision.SELECTED)


def cleanup_installations(
    paths: service_paths.ServicePaths,
    *,
    can_remove: Callable[[Installation], bool] = lambda installation: False,
    check_cancel: Callable[[], None] = release._check_cancel,
) -> list[str]:
    """Confirm no process/pending launch uses a copy under this lock, never from PID files alone.

    The callback must not reenter installation APIs. Future manager integration
    must synchronize this confirmation with its own launch/restart operations.
    """
    with _locked(paths, check_cancel):
        current = _selected(paths)
        _recover_staging(paths)
        removed = []
        for artifact_id in sorted(os.listdir(paths.installations)):
            check_cancel()
            if current is not None and artifact_id == current.artifact_id:
                continue
            installation = _load(paths, artifact_id)
            if can_remove(installation):
                check_cancel()
                obsolete = os.path.join(paths.root, '.staging-' + artifact_id)
                os.rename(installation.root, obsolete)
                _remove(obsolete)
                removed.append(artifact_id)
        return removed
