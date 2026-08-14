import dataclasses
import re

import six

CONFIGURE_CACHE_UNAVAILABLE_MARKER = 'YMAKE_CONFIGURE_CACHE_UNAVAILABLE'
CONFIGURE_CACHE_UNAVAILABLE_PREFIX = f'Error: {CONFIGURE_CACHE_UNAVAILABLE_MARKER} '
REQUIRED_CACHE_KINDS = ('fs', 'conf', 'deps', 'dm')
UNAVAILABLE_REASONS = ('missing', 'incompatible-format', 'updated-binary', 'changed-config', 'read-error', 'unknown')

CONFIGURE_CACHE_UNAVAILABLE_RE = re.compile(
    rf'^{re.escape(CONFIGURE_CACHE_UNAVAILABLE_PREFIX)}'
    rf'cache=(?P<cache>{"|".join(map(re.escape, REQUIRED_CACHE_KINDS))}) '
    rf'reason=(?P<reason>{"|".join(map(re.escape, UNAVAILABLE_REASONS))})\r?\n?$',
)


@dataclasses.dataclass(frozen=True)
class RequiredCacheFailure:
    cache: str
    reason: str
    marker: str | None = None

    def __post_init__(self):
        if self.marker is None:
            object.__setattr__(
                self,
                'marker',
                f'{CONFIGURE_CACHE_UNAVAILABLE_PREFIX}cache={self.cache} reason={self.reason}',
            )


@dataclasses.dataclass(frozen=True)
class UnparsedRequiredCacheFailure:
    marker: str = dataclasses.field(default=CONFIGURE_CACHE_UNAVAILABLE_MARKER, init=False)
    cache: None = dataclasses.field(default=None, init=False)
    reason: None = dataclasses.field(default=None, init=False)


def make_required_cache_failure(cache: str, reason: str) -> RequiredCacheFailure | None:
    if not isinstance(cache, str) or cache not in REQUIRED_CACHE_KINDS:
        return None
    if not isinstance(reason, str) or reason not in UNAVAILABLE_REASONS:
        return None
    return RequiredCacheFailure(cache=cache, reason=reason)


def make_required_cache_failure_from_event(cache: str, outcome: str, reason: str) -> RequiredCacheFailure | None:
    if outcome == 'missing':
        if reason != 'missing':
            return None
    elif outcome == 'rejected':
        if reason == 'missing':
            return None
    else:
        return None
    return make_required_cache_failure(cache, reason)


def parse_ymake_configure_cache_unavailable_line(line: str) -> RequiredCacheFailure | None:
    line = six.ensure_str(line)
    if not line.startswith(CONFIGURE_CACHE_UNAVAILABLE_PREFIX):
        return None
    match = CONFIGURE_CACHE_UNAVAILABLE_RE.fullmatch(line)
    if match is None:
        return None
    return make_required_cache_failure(match.group('cache'), match.group('reason'))


def parse_ymake_configure_cache_unavailable(stderr: str) -> RequiredCacheFailure | None:
    """Compatibility wrapper for callers that still pass complete stderr."""
    for line in six.ensure_str(stderr).splitlines(keepends=True):
        failure = parse_ymake_configure_cache_unavailable_line(line)
        if failure is not None:
            return failure
    return None
