"""Conservative official-release age filtering, not a compatibility proof."""

import datetime
import enum
import typing


class ReleaseOrigin(enum.Enum):
    OFFICIAL = 'official'
    UNOFFICIAL = 'unofficial'
    UNKNOWN = 'unknown'


class ReleaseDate(typing.NamedTuple):
    date: datetime.date | None
    origin: ReleaseOrigin


class AgeCheck(enum.Enum):
    WITHIN_WINDOW = 'within_window'
    OUTSIDE_WINDOW = 'outside_window'
    UNKNOWN = 'unknown'


def check_release_dates(client: ReleaseDate, service: ReleaseDate, *, window: datetime.timedelta) -> AgeCheck:
    """Use calendar-day subtraction, including leap days; equality is allowed.

    Native protocol errors cannot establish which side is outdated, so recovery
    uses an explicit date gate. No default release support duration is set here.
    The client build date is trusted regardless of release provenance.
    Unknown service metadata permits recovery but provides no compatibility guarantee.
    """
    for release in (client, service):
        if release.date is not None and type(release.date) is not datetime.date:
            raise ValueError('release date must be a UTC calendar date')
        if not isinstance(release.origin, ReleaseOrigin):
            raise ValueError('release origin must be explicit')
    if (
        not isinstance(window, datetime.timedelta)
        or window < datetime.timedelta()
        or window.seconds
        or window.microseconds
    ):
        raise ValueError('window must be a nonnegative whole-day timedelta')
    if service.origin != ReleaseOrigin.OFFICIAL or client.date is None or service.date is None:
        return AgeCheck.UNKNOWN
    if service.date - client.date > window:
        return AgeCheck.OUTSIDE_WINDOW
    return AgeCheck.WITHIN_WINDOW
