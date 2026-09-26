# coding: utf-8
"""Progress of a Status as ``progress`` events of a structured display.

The counterpart of TermView for a consumer that reads an event stream instead
of a terminal: no per-task lines, only throttled snapshots of the counters and
of the longest running node. The timing is the schedule of the plain style: a
snapshot goes out when the counters or the node change, at most once per
PROGRESS_RATE_LIMIT_SECONDS, or as a keepalive while tasks are running and
nothing has changed for STILL_WAITING_SCHEDULE seconds (10, then 30, then 60
after each report; the schedule restarts on a change).

What the terminal shows as the body of a finished task (its stderr, e.g. the
compiler warnings) is forwarded as a ``message`` with the node ``path`` and
``kind``, so the stream carries everything the human sees; tasks that ask to be
hidden (``hide_me``) are skipped, like on the terminal.
"""

import collections
import time

from yalibrary import display as display_lib
from yalibrary.status_view import helpers
from yalibrary.status_view import pacer
import yalibrary.formatter

import typing as tp

if tp.TYPE_CHECKING:
    from yalibrary.status_view.status import Status  # noqa


class JsonlProgressView(object):
    def __init__(
        self,
        status,
        display,
        stage='build',
        clock=time.time,
        output_replacements=None,
        patterns=None,
    ):
        # type: (Status, tp.Any, str, tp.Callable[[], float], list | None, tp.Any) -> None
        self._status = status
        self._output_replacements = output_replacements
        self._patterns = patterns
        self._last_id = 0
        self._display = display
        self._stage = stage
        self._clock = clock
        self._last_key = None  # type: tuple | None
        self._pacer = pacer.ProgressPacer(clock())

    def snapshot(self):
        # type: () -> dict
        """Build the progress event for the current state of the status."""
        active = self._status.active()
        event = collections.OrderedDict()
        event['type'] = 'progress'
        event['stage'] = self._stage
        event['active'] = len(active)
        event['done'] = self._status.done()
        event['total'] = self._status.count
        node = self._longest_node(active)
        if node is not None:
            event['node'] = node
        return event

    def tick(self, *extra):
        # type: (*object) -> None
        """Forward new task bodies and write the snapshot if it is due; the signature matches TermView.tick."""
        self._flush_bodies()
        now = self._clock()
        if self._pacer.throttled(now):
            # Nothing can be due yet: skip building the snapshot on this tick.
            return
        snapshot = self.snapshot()
        if self._changed(snapshot):
            self._pacer.note_change(now)
        if self._pacer.due(now, bool(snapshot['active'])):
            self._write(snapshot)

    def finish(self):
        # type: () -> None
        """Write the final snapshot regardless of the intervals, unless it repeats the last one."""
        self._flush_bodies()
        snapshot = self.snapshot()
        if self._changed(snapshot):
            self._write(snapshot)

    def _flush_bodies(self):
        # type: () -> None
        """Emit the stderr of the tasks finished since the last call, as the terminal would print it."""
        for task in self._status.finished(self._last_id):
            self._last_id += 1
            if hasattr(task, 'hide_me') and task.hide_me():
                continue
            if getattr(task, 'exit_code', 0):
                # The stderr of a failed node is the text of its `fail` event.
                continue
            # Most tasks have no body: check it before building the (costly) node view.
            body = task.body() if hasattr(task, 'body') else None
            body = helpers.format_body(body, self._patterns, self._output_replacements)
            text = display_lib.strip_markup(yalibrary.formatter.ansi_codes_to_markup(body)).strip() if body else ''
            if not text:
                continue
            event = collections.OrderedDict()
            event['type'] = 'message'
            event['severity'] = 'info'
            view = task.status() if hasattr(task, 'status') else None
            if isinstance(view, helpers.NodeView):
                event['path'] = view.path
                event['text'] = text
                event['details'] = {'kind': view.kind}
            else:
                event['text'] = text
            self._display.emit_event(event)

    def _changed(self, snapshot):
        # type: (dict) -> bool
        # The elapsed time of the node grows on every tick; on its own it is
        # not a change, the keepalive is what refreshes it.
        return _comparable(snapshot) != self._last_key

    def _write(self, snapshot):
        # type: (dict) -> None
        self._last_key = _comparable(snapshot)
        self._display.emit_event(snapshot)

    @staticmethod
    def _longest_node(active):
        # type: (list) -> dict | None
        # Longest first, so that usually a single node view is built per tick.
        for task, elapsed in sorted(active, key=lambda item: item[1], reverse=True):
            view = task.status() if hasattr(task, 'status') else None
            if isinstance(view, helpers.NodeView):
                break
            # Auxiliary tasks report a plain string: not a build node.
        else:
            return None
        node = collections.OrderedDict()
        node['kind'] = view.kind
        node['path'] = view.path
        node['elapsed'] = int(elapsed)
        return node


def _comparable(snapshot):
    # type: (dict | None) -> tuple | None
    if snapshot is None:
        return None
    node = snapshot.get('node')
    return (
        snapshot['active'],
        snapshot['done'],
        snapshot['total'],
        (node['kind'], node['path']) if node else None,
    )
