import typing
from typing import Any, Callable

import devtools.ya_make.libs.larry_client.py as protocol

from devtools.ya.build.larry_service import address as addresses, release


class RecoveryRequest(typing.NamedTuple):
    endpoint: addresses.ResolvedAddress
    first_attempt_error: Exception
    observed_run_id: str | None = None


def _recoverable(error: Exception) -> bool:
    # Native transport may omit typed metadata on exceptions raised before
    # the failure stage is known; absence means non-recoverable.
    if isinstance(error, protocol.TransportError):
        return getattr(error, 'stage', None) == protocol.FailureStage.CONNECT and getattr(error, 'reason', None) in (
            protocol.TransportFailureReason.ENDPOINT_MISSING,
            protocol.TransportFailureReason.CONNECTION_REFUSED,
        )
    return (
        isinstance(error, protocol.RemoteError)
        and getattr(error, 'stage', None) == protocol.FailureStage.BOOTSTRAP_BEFORE_ACCEPTANCE
        and getattr(error, 'code', None) == 'method_not_found'
    )


class ManagedLarryClient:
    """Give ya-bin one safe recovery after native exchange has cleaned up.

    Generic exts.retry retries execution directly; it cannot express the single
    readiness transition between two attempts or preserve both native failures.
    Keep this policy outside the native transport and OS service managers.

    ensure_service must apply the release-date filter before side effects and
    return only after readiness. This module supplies no production installer.
    """

    def __init__(
        self,
        exchange: Callable[..., Any],
        ensure_service: Callable[[RecoveryRequest], None],
        *,
        observe: Callable[[], str | None] = lambda: None,
        check_cancel: Callable[[], None] = release._check_cancel,
    ) -> None:
        self._exchange = exchange
        self._ensure_service = ensure_service
        self._observe = observe
        self._check_cancel = check_cancel

    def execute(self, address: str | None, *args: Any, **kwargs: Any) -> Any:
        endpoint = addresses.resolve_address(address)
        observed = self._observe() if endpoint.managed else None
        self._check_cancel()
        try:
            return self._exchange(endpoint.address, *args, **kwargs)
        except Exception as error:
            if not endpoint.managed or not _recoverable(error):
                raise
            first_error = error
        # The synchronous exchange has unwound before recovery can run.
        try:
            self._check_cancel()
            self._ensure_service(RecoveryRequest(endpoint, first_error, observed))
            self._check_cancel()
            return self._exchange(endpoint.address, *args, **kwargs)
        except Exception as error:
            error.first_attempt_error = first_error
            error.add_note(f"Original Larry failure: {first_error}")
            raise
