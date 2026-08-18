"""In-process agent console: a JSONL event stream for coding agents.

The console consumes report entries directly through _BuildSink — an
in-process member of the ReportGenerator report list. No temp files,
no tailing, no JsonLineReport involvement.
"""

import contextlib
import json
import logging
import queue
import threading
import time

from collections.abc import Callable, Iterator, Sequence
from typing import IO

import devtools.ya.core.error as core_error

from yalibrary.agent_ui import projection

logger = logging.getLogger(__name__)

_STOP = object()


class AgentConsole:
    """One per command; owns the output stream and the single writer thread."""

    def __init__(self, stream: IO[str], progress_delay: float = 10, running_delay: float = 30) -> None:
        self._stream = stream
        self._progress_delay = progress_delay
        self._running_delay = running_delay
        self._queue: queue.Queue = queue.Queue()
        self._progress: Callable[[], dict | None] | None = None
        self._activity: Callable[[], list] | None = None
        self._last_progress: dict | None = None
        # Only the writer thread reads and writes this: the moment the last
        # event of any type hit the stream.
        self._last_event_time = time.monotonic()
        self._builds = 0
        self._exit_codes: list[int | None] = []
        self._test_counts: dict[str, int] = {}
        self._configure_errors: list[dict] = []
        self._counts_lock = threading.Lock()
        self._stopped = False
        self._thread = threading.Thread(target=self._loop, name='agent-console', daemon=True)

    def start(self) -> None:
        """Start the writer thread."""
        self._thread.start()

    def stop(self) -> None:
        """Emit the summary event and stop the writer thread; idempotent."""
        if self._stopped:
            return
        self._stopped = True
        exit_code = self._summary_exit_code()
        if self._builds == 0 and self._configure_errors:
            # The configuration failed hard before any build frame opened
            # (ConfigurationError in Context.__init__): the buffered errors
            # are the only diagnostics the stream will get. On the soft
            # path (-k) the build frame opens and configure errors reach
            # the stream through the report pipeline, so the buffer is
            # simply dropped there.
            self._emit_configure_errors()
            exit_code = core_error.ExitCodes.CONFIGURE_ERROR
        summary = {'type': 'summary', 'exit_code': exit_code}
        if self._test_counts:
            summary['tests'] = dict(sorted(self._test_counts.items()))
        self.emit(summary)
        self._queue.put(_STOP)
        self._thread.join()

    def buffer_configure_error(self, event: dict) -> None:
        """Buffer a raw NEvent.TDisplayMessage error from the configure stage.

        Called by ConfigureSubscriber from event-queue threads; the buffer
        is emitted only when the configuration fails hard (see stop()).
        """
        self._configure_errors.append(event)

    def _emit_configure_errors(self) -> None:
        emitted: list[dict] = []
        for raw in self._configure_errors:
            try:
                event = projection.project_configure_error(raw)
            except Exception:
                logger.exception("Failed to project a configure error for the agent console")
                continue
            # Several ymake instances report the same error; keep one copy.
            if event in emitted:
                continue
            emitted.append(event)
            self.emit(event)

    @contextlib.contextmanager
    def build(
        self,
        targets: Sequence[str] | None = None,
        platforms: Sequence[str] | None = None,
    ) -> Iterator['_BuildSink']:
        """Frame a single build: emit build_started/artifact/build_finished events around it."""
        index = self._builds
        self._builds += 1
        sink = _BuildSink(self, split_toolchains=len(platforms or []) > 1)
        self.emit(
            {
                'type': 'build_started',
                'build': index,
                'targets': list(targets or []),
                'platforms': list(platforms or []),
            }
        )
        try:
            yield sink
        finally:
            self._emit_final_progress()
            for artifact in sink.artifacts or []:
                self.emit(dict(artifact, type='artifact'))
            self.emit({'type': 'build_finished', 'build': index, 'exit_code': sink.exit_code})
            self._exit_codes.append(sink.exit_code)
            self._last_progress = None
            # The activity source belongs to this build's runner; the runner
            # clears it on its own exit too, this is the frame invariant.
            self._activity = None

    def emit(self, event: dict) -> None:
        """Queue an event for the writer thread."""
        self._queue.put(event)

    def count_tests(self, statuses: Sequence[str]) -> None:
        """Accumulate test case statuses for the summary event."""
        with self._counts_lock:
            for status in statuses:
                self._test_counts[status] = self._test_counts.get(status, 0) + 1

    def set_progress(self, functor: Callable[[], dict | None]) -> None:
        """Install the progress source and emit the initial snapshot right away.

        The first snapshot announces the work totals; later snapshots come
        from the heartbeat and only when something has changed.
        """
        try:
            event = projection.project_progress(functor())
        except Exception:
            logger.exception("Failed to compute the initial progress for the agent console")
            event = None
        if event:
            # The heartbeat cannot run concurrently: _progress is still None.
            self._last_progress = event
            self.emit(event)
        self._progress = functor

    def set_activity(self, functor: Callable[[], list] | None) -> None:
        """Install (or clear) the source of currently running tasks.

        Set by the local runner to its Status.active — the same snapshot
        the human ticker renders as "running for N secs". After
        running_delay of complete stream silence the writer thread projects
        it into a standalone ``running`` event, so the agent sees both that
        ya is alive and what it is busy with. Progress events stay
        counters-only.
        """
        self._activity = functor

    def _running_snapshot(self) -> dict | None:
        activity = self._activity
        if activity is None:
            return None
        try:
            return projection.project_running(activity())
        except Exception:
            logger.exception("Failed to snapshot running tasks for the agent console")
            return None

    def _emit_final_progress(self) -> None:
        """Flush the definitive progress ignoring the heartbeat timing.

        The heartbeat fires only in quiet windows, so by the end of the
        build the last emitted snapshot is usually stale.
        """
        progress = self._progress
        # Detach the source first to stop the heartbeat; a heartbeat already
        # in flight may at worst emit the same final snapshot twice.
        self._progress = None
        if progress is None:
            return
        try:
            event = projection.project_progress(progress())
        except Exception:
            logger.exception("Failed to compute the final progress for the agent console")
            return
        if event and event != self._last_progress:
            self._last_progress = event
            self.emit(event)

    def _summary_exit_code(self) -> int | None:
        for exit_code in self._exit_codes:
            if exit_code:
                return exit_code
        return self._exit_codes[-1] if self._exit_codes else None

    def _heartbeat(self) -> list[dict]:
        """Quiet-window pulse: a changed progress snapshot and/or a running hint."""
        events = []
        progress_event = self._progress_heartbeat()
        if progress_event:
            events.append(progress_event)
        running_event = self._running_heartbeat()
        if running_event:
            events.append(running_event)
        return events

    def _progress_heartbeat(self) -> dict | None:
        progress = self._progress
        if progress is None:
            return None
        try:
            event = projection.project_progress(progress())
        except Exception:
            logger.exception("Failed to compute progress heartbeat for the agent console")
            return None
        # Only the writer thread reads and writes _last_progress.
        if event == self._last_progress:
            return None
        self._last_progress = event
        return event

    def _running_heartbeat(self) -> dict | None:
        """The keepalive during a long test suite or link.

        Fires only after running_delay of complete stream silence: any
        written event resets the countdown, so a stream that is already
        alive never carries the hint. The hint is an event itself, which
        makes it self-throttling — one pulse per running_delay of stall.
        """
        if self._activity is None:
            return None
        if time.monotonic() - self._last_event_time < self._running_delay:
            return None
        snapshot = self._running_snapshot()
        if not snapshot:
            return None
        return dict(snapshot, type='running')

    def _loop(self) -> None:
        while True:
            try:
                events = [self._queue.get(timeout=self._progress_delay)]
            except queue.Empty:
                events = self._heartbeat()
            for event in events:
                if event is _STOP:
                    return
                self._last_event_time = time.monotonic()
                try:
                    # Agents read raw JSONL: keep "type" the first key of
                    # every line regardless of how the event was built.
                    event = {'type': event['type'], **event}
                    self._stream.write(json.dumps(event) + '\n')
                    self._stream.flush()
                except Exception:
                    # A write failure must never propagate into the build.
                    logger.exception("Failed to write an agent console event")


class _BuildSink:
    """Report-list sink + the place where the build records its outcome.

    The protocol (set_progress_channel, __call__(entries), finish*,
    trace_stage) is modeled on StoredReport in
    devtools/ya/build/reports/results_report.py and is driven by
    ReportGenerator in devtools/ya/build/reports/autocheck_report.py.
    """

    _TEST_KINDS = frozenset(('test', 'style'))

    def __init__(self, console: AgentConsole, split_toolchains: bool = False) -> None:
        self._console = console
        self._split_toolchains = split_toolchains
        self._tests_started = False
        self._tests_started_lock = threading.Lock()
        self.exit_code: int | None = None
        self.artifacts: list[dict] | None = None

    def __call__(self, entries: Sequence[dict]) -> None:
        # Called from listener/worker threads by ReportGenerator._add_entries —
        # must be cheap and never raise.
        try:
            statuses = [status for status in map(projection.test_case_status, entries) if status]
            if statuses:
                self._console.count_tests(statuses)
            for entry in entries:
                event = projection.project_result(entry)
                if event:
                    if not self._split_toolchains:
                        # With a single platform the toolchain repeats on
                        # every event and carries no information.
                        event.pop('toolchain', None)
                    self._mark_tests_started(event)
                    # No per-event build index: results are framed by the
                    # build_started/build_finished events, which carry it.
                    self._console.emit(event)
        except Exception:
            # Exception wall: a bug here must never fail the build.
            logger.exception("Failed to project report entries for the agent console")

    def _mark_tests_started(self, event: dict) -> None:
        """Emit a one-shot tests_started marker before the first test result.

        Test nodes run in the same graph as compilation, so this marks the
        moment test results begin to arrive, not the end of compilation:
        build results may still follow.
        """
        if event.get('kind') not in self._TEST_KINDS or self._tests_started:
            return
        with self._tests_started_lock:
            if self._tests_started:
                return
            self._tests_started = True
            self._console.emit({'type': 'tests_started'})

    def set_progress_channel(self, functor: Callable[[], dict | None]) -> None:
        """Announce the work totals and keep the source for the heartbeat."""
        self._console.set_progress(functor)

    def finish(self) -> None:
        pass

    def finish_style_report(self) -> None:
        pass

    def finish_configure_report(self) -> None:
        pass

    def finish_build_report(self) -> None:
        pass

    def finish_tests_report(self) -> None:
        pass

    def finish_tests_report_by_size(self, size: str) -> None:
        pass

    def trace_stage(self, build_stage: str) -> None:
        pass
