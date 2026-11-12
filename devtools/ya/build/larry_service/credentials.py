"""Publish credentials for future YT sessions without touching active ones."""

import enum
from typing import Any, Callable

from devtools.ya.build.larry_service import installation, paths as service_paths, release, state_files


class CredentialUpdate(enum.Enum):
    PUBLISHED = 'published'
    PRESERVED = 'preserved'


def update_credentials(
    paths: service_paths.ServicePaths, token: str | None, *, check_cancel: Callable[[], None] = release._check_cancel
) -> CredentialUpdate:
    """Publish an already selected token; absent credentials never erase a file."""
    check_cancel()
    if token is not None and not isinstance(token, str):
        raise ValueError('YT credentials must be text')
    # Match the reader's BOM/outer-whitespace handling before deciding absence.
    token = (token or '').removeprefix('\ufeff').strip()
    if not token:
        return CredentialUpdate.PRESERVED
    contents = token.encode('utf-8')
    with installation._locked(paths, check_cancel):
        state_files.write_private_file(paths.token_file, contents, check_cancel=check_cancel)
    return CredentialUpdate.PUBLISHED


def prepare_credentials(
    paths: service_paths.ServicePaths,
    options: Any,
    *,
    token_resolver: Callable[[Any], str | None],
    check_cancel: Callable[[], None] = release._check_cancel,
) -> CredentialUpdate:
    """Use ya's existing selection policy only when the caller enabled YT."""
    check_cancel()
    if not options.yt_store:
        return CredentialUpdate.PRESERVED
    token = token_resolver(options)
    return update_credentials(paths, token, check_cancel=check_cancel)
