# coding: utf-8
"""When to report progress: the schedule shared by the plain and jsonl styles.

The numbers follow Bazel. Progress on change is rate limited: Bazel prints at
most once a second (NO_CURSES_MINIMAL_PROGRESS_RATE_LIMIT of UiEventHandler),
a consumer that reads the whole log does not need that many lines, one every
ten seconds keeps a long build legible. Silence is broken by keepalives on the
schedule of ActionExecutionStatusReporter with --progress_report_interval left
at 0: the first report after 10 seconds, the next after 30, then once a minute.
"""

import typing as tp  # noqa

PROGRESS_RATE_LIMIT_SECONDS = 10.0
# Indexed by the number of reports already made, capped.
STILL_WAITING_SCHEDULE = (10, 30, 60)


def wait_seconds(reports_made):
    # type: (int) -> int
    """Seconds of silence before the next keepalive."""
    return STILL_WAITING_SCHEDULE[min(reports_made, len(STILL_WAITING_SCHEDULE) - 1)]


class ProgressPacer(object):
    """When to report progress; what to report is up to the caller.

    A change is reported at most once per PROGRESS_RATE_LIMIT_SECONDS; while
    nothing changes and tasks are running, a keepalive goes out after
    STILL_WAITING_SCHEDULE seconds of silence. Any report resets the rate
    limit; a change restarts the keepalive schedule.
    """

    CHANGE = 'change'
    KEEPALIVE = 'keepalive'

    def __init__(self, now):
        # type: (float) -> None
        self._last_report_time = None  # type: float | None
        self._silence_since = now
        self._reports_made = 0
        self._pending = False

    def note_change(self, now):
        # type: (float) -> None
        """Something changed: report it when the rate limit allows, restart the keepalive schedule."""
        self._silence_since = now
        self._reports_made = 0
        self._pending = True

    def throttled(self, now):
        # type: (float) -> bool
        """Nothing can be due on this tick: a caller with a costly state can skip building it."""
        return self._last_report_time is not None and now - self._last_report_time < PROGRESS_RATE_LIMIT_SECONDS

    def due(self, now, active):
        # type: (float, bool) -> str | None
        """CHANGE or KEEPALIVE to report on this tick, None to stay quiet."""
        if self.throttled(now):
            return None
        if self._pending:
            self._pending = False
            self._last_report_time = now
            return self.CHANGE
        if active and now - self._silence_since >= wait_seconds(self._reports_made):
            self._silence_since = now
            self._reports_made += 1
            self._last_report_time = now
            return self.KEEPALIVE
        return None
