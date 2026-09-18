# coding: utf-8
"""Rendering helpers of the plain output style.

The plain style is a log for non-interactive consumers such as coding agents
and CI: every line that is not a progress report starts with a severity prefix,
nothing is colored, nothing is rewritten in place and nothing is trimmed to
the terminal width.

Progress is shaped after the non-curses UI of Bazel (UiEventHandler,
UiStateTracker and ActionExecutionStatusReporter in bazelbuild/bazel): a
progress line only when the number of finished tasks changed and at most
once in a while, and a "Still waiting" report with a growing interval when
nothing finishes.
"""

import typing as tp  # noqa: F401

import devtools.ya.core.error as core_error
import devtools.ya.test.const

from yalibrary import display as display_lib

# The value of --output-style that selects this style.
STYLE = 'plain'

DEBUG = 'DEBUG'
INFO = 'INFO'
WARNING = 'WARNING'
ERROR = 'ERROR'
FATAL = 'FATAL'
FAILED = 'FAILED'

# Severity names of ymake messages (msgTypesAsString in devtools/ymake/diag/display.cpp)
# and of the display log handler, to the prefixes of this style.
SEVERITY_BY_NAME = {
    'Debug': DEBUG,
    'Info': INFO,
    'Warn': WARNING,
    'Error': ERROR,
    'Fatal': FATAL,
}

# One sentence per exit code: what the consumer should do next. The long
# advice of the agent event stream lives in yalibrary/agent_ui/classify.py;
# this table is its terse, terminal-sized counterpart.
_HINT_BY_EXIT_CODE = {
    core_error.ExitCodes.GENERIC_ERROR: 'see the ERROR lines above',
    core_error.ExitCodes.UNHANDLED_EXCEPTION: 'ya itself crashed, the traceback is in the ya log',
    core_error.ExitCodes.USAGE_ERROR: 'fix the command line, `ya <subcommand> --help` lists what is accepted',
    core_error.ExitCodes.CONFIGURE_ERROR: 'fix the configure errors reported above and rerun',
    core_error.ExitCodes.NO_TESTS_COLLECTED: (
        'no tests were collected: check the target path, the -F filter and the test sizes (-t, -tt, -ttt)'
    ),
    core_error.ExitCodes.TEST_FAILED: 'fix the failed tests listed above',
    core_error.ExitCodes.INFRASTRUCTURE_ERROR: 'temporary infrastructure error, rerun the same command',
    core_error.ExitCodes.NOT_RETRIABLE_ERROR: 'rerunning will not help, read the error above',
    core_error.ExitCodes.YT_STORE_FETCH_ERROR: 'distributed cache fetch failed, rerun or add --no-yt-store',
}


def line(severity, text):
    # type: (str, str) -> str
    return '{}: {}'.format(severity, text)


def header(task_status):
    # type: (object) -> str
    """Render the status of a task as one plain line: no markup, no trimming."""
    if not isinstance(task_status, str):
        # NodeView yields its variants from the most detailed one; the terminal
        # status line takes the first that fits, the plain line always takes it.
        task_status = next(iter(task_status), '')
    return display_lib.strip_markup(task_status)


def is_failed(task):
    # type: (object) -> bool
    return bool(getattr(task, 'exit_code', 0))


def is_test(task):
    # type: (object) -> bool
    """Whether the task ran a test suite.

    The size marker alone does not tell: the test listing node of `-L` is
    marked `TL` too (inject_list_result_node in devtools/ya/test/test_node).
    Test suite nodes are the ones built by get_test_kv, which carries `path`.
    """
    kv = getattr(task, 'kv', None) or {}
    return bool(devtools.ya.test.const.TestSize.is_test_shorthand(kv.get('p')) and kv.get('path'))


def severity_of(task):
    # type: (object) -> str
    if is_failed(task):
        return ERROR
    kv = getattr(task, 'kv', None) or {}
    if kv.get('show_out'):
        # The graph declares the stderr of the node as its useful output:
        # the test listing, canonization, fuzzing.
        return INFO
    # A passed build node with stderr: compiler warnings and the like.
    return WARNING


# Bazel prints at most once a second (NO_CURSES_MINIMAL_PROGRESS_RATE_LIMIT of
# UiEventHandler); a consumer that reads the whole log does not need that
# many lines, one every ten seconds keeps a long build legible.
PROGRESS_RATE_LIMIT_SECONDS = 10.0
# SHOW_TIME_THRESHOLD_SECONDS of UiStateTracker: the elapsed time of a task
# is shown only once it is worth noticing.
PROGRESS_SHOW_TIME_THRESHOLD_SECONDS = 3
# Schedule of ActionExecutionStatusReporter when --progress_report_interval
# is left at 0: the first report after 10 seconds, the next after 30, then
# once a minute. Indexed by the number of reports already made, capped.
STILL_WAITING_SCHEDULE = (10, 30, 60)
# MAX_LINES of ActionExecutionStatusReporter.
STILL_WAITING_MAX_LINES = 10


def wait_seconds(reports_made):
    # type: (int) -> int
    """Seconds of silence before the next "Still waiting" report."""
    return STILL_WAITING_SCHEDULE[min(reports_made, len(STILL_WAITING_SCHEDULE) - 1)]


def progress(done, total, text, elapsed=0.0, running=1):
    # type: (int, int, str, float, int) -> str
    """`[done / total] longest running task; Ns ... (K actions running)`, the short progress bar of Bazel."""
    line = '[{} / {}] {}'.format(done, total, text)
    if elapsed > PROGRESS_SHOW_TIME_THRESHOLD_SECONDS:
        line += '; {}s'.format(int(elapsed))
    if running > 1:
        line += ' ... ({} actions running)'.format(running)
    return line


def still_waiting(jobs):
    # type: (list) -> str
    """The report on what is still running: `(text, elapsed)` pairs, longest running first."""
    count = len(jobs)
    lines = ['Still waiting for {} job{} to complete:'.format(count, '' if count == 1 else 's')]
    for text, elapsed in jobs[:STILL_WAITING_MAX_LINES]:
        lines.append('    {}, {} s'.format(text, int(elapsed)))
    if count > STILL_WAITING_MAX_LINES:
        lines.append('    ... {} more jobs'.format(count - STILL_WAITING_MAX_LINES))
    return '\n'.join(lines)


class PlainProgress(object):
    """What to print about the running tasks on a tick, if anything.

    Keeps the count of finished tasks last seen (resets the silence clock)
    and last reported (rate limited), and the number of "Still waiting"
    reports made since the last change.
    """

    def __init__(self, now):
        # type: (float) -> None
        self._seen_done = 0
        self._reported_done = 0
        self._last_line_time = 0
        self._last_change_time = now
        self._reports_made = 0

    def tick(self, now, done, total, active, running):
        # type: (float, int, int, int, tp.Callable[[], list]) -> str | None
        """The progress line or the "Still waiting" report to print, if any.

        `active` is the number of running tasks; `running` yields their
        `(header, elapsed)` pairs, longest running first, and is called only
        when something is printed: ticks are frequent, lines are rare.
        """
        if done != self._seen_done:
            self._seen_done = done
            self._last_change_time = now
            self._reports_made = 0
        if not active:
            return None
        if done != self._reported_done and now - self._last_line_time >= PROGRESS_RATE_LIMIT_SECONDS:
            self._reported_done = done
            self._last_line_time = now
            header, elapsed = running()[0]
            return progress(done, total, header, elapsed, active)
        if now - self._last_change_time >= wait_seconds(self._reports_made):
            self._last_change_time = now
            self._reports_made += 1
            return still_waiting(running())
        return None


def final_message(exit_code):
    # type: (int) -> str
    """The last line of a run: one fixed-format verdict a consumer can match on."""
    if not exit_code:
        return line(INFO, 'Build completed successfully')
    hint = _HINT_BY_EXIT_CODE.get(exit_code, _HINT_BY_EXIT_CODE[core_error.ExitCodes.GENERIC_ERROR])
    return line(FAILED, 'Build did NOT complete successfully (exit code {}): {}'.format(exit_code, hint))
