"""In-process agent console: a JSONL event stream for coding agents.

The console consumes report entries directly through _BuildSink — an
in-process member of the ReportGenerator report list. No temp files,
no tailing, no JsonLineReport involvement.
"""

import contextlib
import dataclasses
import json
import logging
import queue
import threading
import time

from collections.abc import Callable, Iterator, Sequence
from typing import IO

import devtools.ya.core.error as core_error

from devtools.ya.yalibrary.agent_ui import classify
from devtools.ya.yalibrary.agent_ui import projection

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
        self._configure_failed = False
        self._outcome: classify.Outcome | None = None
        self._counts_lock = threading.Lock()
        self._stopped = False
        self._thread = threading.Thread(target=self._loop, name='agent-console', daemon=True)

    def start(self) -> None:
        """Start the writer thread."""
        self._thread.start()

    def set_outcome(self, exit_code: int | None, exception: BaseException | None = None) -> None:
        """Record the real outcome of the run, as seen by the module stack.

        The build aggregate only knows about the builds that opened a
        frame; the outcome knows about the whole command, so once it is
        recorded the summary reports it instead. The exception, when the
        run died of one, becomes the summary text.
        """
        self._outcome = classify.Outcome(exit_code=exit_code, exception=exception)

    def stop(self) -> None:
        """Emit the summary event and stop the writer thread; idempotent."""
        if self._stopped:
            return
        self._stopped = True
        exit_code = self._summary_exit_code()
        # The hard path: the configuration died before any build frame opened.
        # On the soft path (--keep-going) the failure is noted by the sink
        # instead, from the configure entries of the report.
        configure_died = self._builds == 0 and bool(self._configure_errors)
        configure_failed = configure_died or self._configure_failed
        if configure_died:
            # The configuration failed hard before any build frame opened
            # (ConfigurationError in Context.__init__): the buffered errors
            # are the only diagnostics the stream will get. On the soft
            # path (-k) the build frame opens and configure errors reach
            # the stream through the report pipeline, so the buffer is
            # simply dropped there.
            self._emit_configure_errors()
            exit_code = core_error.ExitCodes.CONFIGURE_ERROR
        if self._outcome is not None:
            exit_code = self._outcome.exit_code
        summary = {'type': 'summary', 'exit_code': exit_code}
        # The verdict does not depend on how the code became known: a build
        # that failed on its own is as diagnosable as an outcome pushed in
        # from the module stack.
        outcome = self._outcome if self._outcome is not None else classify.Outcome(exit_code=exit_code)
        summary.update(self._describe_outcome(dataclasses.replace(outcome, configure_failed=configure_failed)))
        if self._test_counts:
            summary['tests'] = dict(sorted(self._test_counts.items()))
        self.emit(summary)
        self._queue.put(_STOP)
        self._thread.join()

    def _describe_outcome(self, outcome: classify.Outcome) -> dict:
        """Turn the recorded outcome into the verdict fields of the summary.

        A description that cannot be produced must not take the summary down
        with it, so the exception wall keeps the exit code reportable.
        """
        fields: dict = {}
        try:
            verdict = classify.classify(outcome)
            if verdict is not None:
                fields['category'] = verdict.category
                fields['action'] = verdict.action
            if outcome.exception is not None:
                text = projection.plain_message_text(str(outcome.exception))
                if text:
                    fields['text'] = text
        except Exception:
            logger.exception("Failed to describe the run outcome for the agent console")
        return fields

    def note_configure_failed(self) -> None:
        """Record that the run collected configure errors.

        Called by the sink for the soft (--keep-going) path, where the build
        runs on and the failure would otherwise be visible only as individual
        result events. The summary verdict reads this instead of the exit
        code, which does not report a broken configuration yet (YA-1456).
        """
        self._configure_failed = True

    def buffer_configure_error(self, event: dict) -> None:
        """Buffer a raw NEvent.TDisplayMessage error from the configure stage.

        Called by ConfigureSubscriber from event-queue threads; the buffer
        is emitted only when the configuration fails hard (see stop()).
        Once a build frame has opened the buffer can never be read again,
        so it stops accepting entries instead of hoarding dead dicts.
        """
        if self._builds:
            return
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
        # Configure errors reach the stream through the report pipeline now
        # (see buffer_configure_error); drop the buffer.
        self._configure_errors = []
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
        # The heartbeat cannot run concurrently: _progress is still None.
        self._emit_progress_if_changed(functor, 'initial')
        self._progress = functor

    def set_activity(self, functor: Callable[[], list] | None) -> None:
        """Install (or clear) the source of currently running tasks.

        Set by the local runner to its Status.active — the same snapshot
        the human ticker renders as "running for N secs". After
        running_delay of complete stream silence the writer thread projects
        it into a standalone ``running`` event, so the agent sees both that
        ya is alive and what it is busy with. Progress events stay
        counters-only; the same source feeds their in_flight counter.
        """
        self._activity = functor

    @contextlib.contextmanager
    def temporary_activity(self, functor: Callable[[], list]) -> Iterator[None]:
        """Temporarily replace the running-task source and restore it on exit."""
        previous = self._activity
        self._activity = functor
        try:
            yield
        finally:
            if self._activity is functor:
                self._activity = previous

    def _in_flight(self) -> int:
        """Count the tasks the runner is executing right now, 0 without a source."""
        activity = self._activity
        if activity is None:
            return 0
        try:
            return len(activity())
        except Exception:
            logger.exception("Failed to count in-flight tasks for the agent console")
            return 0

    def _running_snapshot(self) -> dict | None:
        activity = self._activity
        if activity is None:
            return None
        try:
            return projection.project_running(activity())
        except Exception:
            logger.exception("Failed to snapshot running tasks for the agent console")
            return None

    def _emit_progress_if_changed(self, source: Callable[[], dict | None], what: str) -> None:
        """Project the progress source and emit the snapshot unless it repeats the last one.

        in_flight is part of the comparison — a change in the task count
        alone is worth a snapshot. The wall-clock stamp is added later, in
        the writer loop, so it never defeats the dedup. Callers alternate
        between the build thread (initial/final, when the heartbeat source
        is detached) and the writer thread (heartbeat), never concurrently.
        """
        try:
            event = projection.project_progress(source(), self._in_flight())
        except Exception:
            logger.exception("Failed to compute the %s progress for the agent console", what)
            return
        if event and event != self._last_progress:
            self._last_progress = event
            self.emit(event)

    def _emit_final_progress(self) -> None:
        """Flush the definitive progress ignoring the heartbeat timing.

        The heartbeat fires only in quiet windows, so by the end of the
        build the last emitted snapshot is usually stale.
        """
        progress = self._progress
        # Detach the source first to stop the heartbeat; a heartbeat already
        # in flight may at worst emit the same final snapshot twice.
        self._progress = None
        if progress is not None:
            self._emit_progress_if_changed(progress, 'final')

    def _summary_exit_code(self) -> int | None:
        for exit_code in self._exit_codes:
            if exit_code:
                return exit_code
        return self._exit_codes[-1] if self._exit_codes else None

    def _heartbeat(self) -> None:
        """Quiet-window pulse: a changed progress snapshot and/or a running hint."""
        progress = self._progress
        if progress is not None:
            self._emit_progress_if_changed(progress, 'heartbeat')
        running_event = self._running_heartbeat()
        if running_event:
            self.emit(running_event)

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
                event = self._queue.get(timeout=self._progress_delay)
            except queue.Empty:
                self._heartbeat()
                continue
            if event is _STOP:
                return
            self._last_event_time = time.monotonic()
            try:
                # Agents read raw JSONL: keep "type" the first key of
                # every line regardless of how the event was built.
                # The wall-clock stamp is added here, past every dedup
                # comparison: events are deduped by content, and a stamp
                # applied earlier would make identical events unequal.
                event = {'type': event['type'], 'ts': round(time.time(), 3), **event}
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
            statuses = []
            for entry in entries:
                if projection.is_configure_failure(entry):
                    self._console.note_configure_failed()
                status = projection.test_case_status(entry)
                if status:
                    statuses.append(status)
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
            if statuses:
                self._console.count_tests(statuses)
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
