import re
import typing

import devtools.ya_make.libs.larry_client.py as protocol


class ResolvedAddress(typing.NamedTuple):
    address: str | None
    managed: bool


def resolve_address(address: str | None) -> ResolvedAddress:
    # Preserve original ownership: an explicit endpoint must never grant
    # permission to install or restart a service, even when its path matches.
    if address == 'grpc:local:':
        return ResolvedAddress('grpc:local:ya_make/larry-grpc-1', managed=True)
    if address == 'local:':
        return ResolvedAddress(f'local:ya_make/larry-{protocol.TRANSPORT_VERSION}', managed=True)
    return ResolvedAddress(address, managed=False)


def managed_transport_version(address: str) -> int | None:
    """Read only the canonical managed namespace, never an explicit address policy."""
    match = re.fullmatch(r'(?:grpc:local:ya_make/larry-grpc-|local:ya_make/larry-)([0-9]+)', address)
    return int(match[1]) if match else None
