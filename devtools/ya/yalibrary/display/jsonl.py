# coding: utf-8
"""Machine-readable display: one JSON object per line on the given stream.

Replaces the human display for --output-style=jsonl. Every event line starts
with the keys ``type`` and ``ts`` (wall-clock seconds), the rest of the event
follows flat. The display counts failures and warnings on the fly and reports them in the
final ``finished`` event together with the exit code, its category and the advice on what
to do about the outcome.
"""

from __future__ import print_function

import collections
import hashlib
import json
import threading
import time
import typing as tp  # noqa

import six

import devtools.ya.core.error as core_error
import yalibrary.display


class JsonlDisplay(object):
    structured = True

    def __init__(self, stream, handler, clock=time.time):
        # type: (tp.IO[str], str, tp.Callable[[], float]) -> None
        self._stream = stream
        self._clock = clock
        self._lock = threading.Lock()
        self._started_at = clock()
        self._closed = False
        # list of tuples (stream, data)
        self._dump_after_close = []
        self._fails_by_stage = collections.Counter()
        self._warnings = 0
        self._test_counts = None
        self._seen_messages = set()
        self.emit_event({'type': 'started', 'handler': handler})

    def emit_event(self, event):
        # type: (dict) -> None
        """Write one event line, keeping the per-run counters and message dedup."""
        with self._lock:
            if self._closed:
                return
            if not self._account(event):
                return
            self._write(event)

    def emit_message(self, msg='', severity='info'):
        # type: (str, str) -> None
        text = yalibrary.display.strip_markup(msg).strip()
        if not text:
            return
        self.emit_event({'type': 'message', 'severity': severity, 'text': text})

    def emit_status(self, *parts):
        # type: (*str) -> None
        pass

    def dump_after_close(self, stream, data):
        # type: (tp.IO[str], str) -> None
        self._dump_after_close.append((stream, data))

    def record_test_counts(self, counts):
        # type: (dict[str, int]) -> None
        """Remember test status counters for the ``finished`` event."""
        self._test_counts = dict(counts)

    def close(self, exit_code=None, exception=None):
        # type: (int | None, BaseException | None) -> None
        """Write the ``finished`` event once and flush the deferred dumps."""
        with self._lock:
            if self._closed:
                return
            self._closed = True
            self._write(self._finished_event(exit_code, exception))
        for stream, data in self._dump_after_close:
            stream.write(data)

    def _account(self, event):
        # type: (dict) -> bool
        """Update counters for the event; return False if it must be dropped."""
        event_type = event['type']
        if event_type == 'message':
            # The text may be a whole compiler stderr: keep its digest, not the text.
            digest = hashlib.sha1(six.ensure_binary(event.get('text') or '')).digest()
            key = (event.get('severity'), event.get('path'), digest)
            if key in self._seen_messages:
                return False
            self._seen_messages.add(key)
            if event.get('severity') == 'warning':
                self._warnings += 1
        elif event_type == 'fail':
            self._fails_by_stage[event.get('stage')] += 1
        return True

    def _finished_event(self, exit_code, exception):
        # type: (int | None, BaseException | None) -> dict
        counters = collections.OrderedDict()
        if self._test_counts is not None:
            counters['tests'] = self._test_counts
        for name, value in (
            ('build_failed', self._fails_by_stage['build']),
            ('configure_errors', self._fails_by_stage['configure']),
            ('warnings', self._warnings),
        ):
            if value:
                counters[name] = value
        event = collections.OrderedDict()
        event['type'] = 'finished'
        event['exit_code'] = exit_code
        event['category'] = _exit_code_name(exit_code)
        event['duration'] = round(self._clock() - self._started_at, 3)
        event['counters'] = counters
        if exception is not None:
            event['text'] = yalibrary.display.strip_markup(str(exception)).strip()
        advice = _advice(event['category'], configure_failed=bool(self._fails_by_stage['configure']))
        if advice is not None:
            event['advice'] = advice
        return event

    def _write(self, event):
        # type: (dict) -> None
        line = collections.OrderedDict()
        line['type'] = event['type']
        line['ts'] = round(self._clock(), 3)
        for key, value in event.items():
            if key != 'type':
                line[key] = value
        self._stream.write(json.dumps(line) + '\n')
        self._stream.flush()


def _exit_code_name(code):
    # type: (int | None) -> str | None
    """Return the snake_case name of the ExitCodes constant, None for success."""
    if not code:
        return None
    for name, value in vars(core_error.ExitCodes).items():
        if not name.startswith('_') and value == code:
            return name.lower()
    return 'generic_error'


# What to do about the outcome is a function of the category alone; it travels
# as a separate field so that the consumer does not have to know the table. The
# advice is prose rather than a slug: a slug would only repeat the category,
# while the next step turns on things a name cannot hold: where the failure
# can hide, which events carry the diagnosis, whether a rerun is worth anything.
_ADVICE_BY_CATEGORY = {
    'generic_error': (
        "The run failed without an exit code of its own. The failed build and test events of this stream, "
        "and the `text` field of this summary, carry the diagnosis: read them and fix what they name. "
        "Rerunning the same command unchanged fails the same way."
    ),
    'unhandled_exception': (
        "ya itself crashed: the failure is in the build system, not in the code being built. The `text` field "
        "holds the exception, the full traceback is in the ya log (`$YA_CACHE_DIR/logs`, `~/.ya/logs` by "
        "default). Do not rework the command around the crash; report it with the log."
    ),
    'configure_error': (
        "Configuration failed, so the build description has to be fixed before anything else. The error is not "
        "necessarily in the ya.make of the target: it can come from any file the configuration pulls in, such as an "
        ".inc, a macro, or the ya.make of a module reached through PEERDIR or RECURSE. Read the configure "
        "events of this stream for the file and the line they name instead of assuming the target's own "
        "ya.make. Build and test failures collected under a broken configuration may disappear once it is "
        "fixed, so fix the configuration first and rerun."
    ),
    'no_tests_collected': (
        "Tests were requested but none was collected, so nothing ran and nothing is known to be broken. What "
        "needs fixing is the command, not the code: check the target path, the `-F` filter, and the test sizes "
        "the run allows (`-t`, `-tt`, `-ttt`). `ya test -L` on the target lists what there is to run."
    ),
    'test_failed': (
        "Tests ran and some of them failed. Every failure is a separate event of this stream carrying `path`, "
        "`name` and the `text` of the failure; fix the code or the test they name. A rerun of the same "
        "command fails the same way, so change something before rerunning."
    ),
    'infrastructure_error': (
        "The run died of an error ya treats as temporary (network, disk space, a service that was briefly "
        "unavailable), not of anything in the code. Rerun the same command as is. If the failure repeats, the "
        "environment is what to look at (free space, network access, tokens), still not the command."
    ),
    # Not retriable is the opposite of infrastructure_error: the same run fails
    # the same way, so the error itself has to be looked at.
    'not_retriable_error': (
        "The run died of an error explicitly marked as not retriable: the same command fails the same way, so "
        "a rerun buys nothing. Read the `text` field of this summary: if it names something the command or "
        "the code can fix, fix that; otherwise report the failure together with the ya log."
    ),
    'yt_store_fetch_error': (
        "A node could not be fetched from the distributed cache; the command itself is fine. Rerun it as is; "
        "the cache is usually reachable on the next attempt. If it keeps failing, `--no-yt-store` gets the run "
        "through by building everything locally."
    ),
    'usage_error': (
        "ya rejected the command line itself, so nothing was configured or built: an unknown option, a value "
        "it does not accept, or a target that is not a path in the repository. Fix the invocation ("
        "`ya <subcommand> --help` lists what is accepted) and leave the code alone."
    ),
}


def _advice(category, configure_failed):
    # type: (str | None, bool) -> str | None
    """Return what to do about the outcome, None when there is nothing to act on."""
    if configure_failed:
        # A broken configuration is diagnosed by the configure failures the
        # display counted, not by the exit code: ignore_configure_errors still
        # defaults to true (see _calc_exit_code in build/ya_make.py and
        # YA-1456), so such a run exits 1 without --keep-going and 0 with it,
        # and CONFIGURE_ERROR never arrives. Reading the fact keeps the advice
        # the same before and after that default is flipped and closes the
        # false green of a --keep-going run whose configuration failed.
        category = 'configure_error'
    return _ADVICE_BY_CATEGORY.get(category)
