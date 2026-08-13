import dataclasses
import re

import six

CONFIGURE_CACHE_UNAVAILABLE_RE = re.compile(
    r'^Error: YMAKE_CONFIGURE_CACHE_UNAVAILABLE '
    r'cache=(?P<cache>fs|conf|deps|dm) '
    r'reason=(?P<reason>missing|incompatible-format|updated-binary|changed-config|read-error|unknown)\r?$',
    re.MULTILINE,
)


@dataclasses.dataclass(frozen=True)
class RequiredCacheFailure:
    cache: str
    reason: str
    marker: str


def parse_ymake_configure_cache_unavailable(stderr: str) -> RequiredCacheFailure | None:
    match = CONFIGURE_CACHE_UNAVAILABLE_RE.search(six.ensure_str(stderr))
    if match is None:
        return None
    return RequiredCacheFailure(
        cache=match.group('cache'),
        reason=match.group('reason'),
        marker=match.group(0).rstrip('\r'),
    )
