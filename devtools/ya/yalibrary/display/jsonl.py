# coding: utf-8
"""Machine-readable display: one JSON object per line on the given stream.

Replaces the human display for --output-style=jsonl. Every event line starts
with the keys ``type`` and ``ts`` (wall-clock seconds), the rest of the event
follows flat. The display counts failures and warnings on the fly and reports them in the
final ``finished`` event.
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
