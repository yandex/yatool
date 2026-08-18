"""Event-queue subscriber that projects the live configure stage into the agent stream.

Modeled on PrintProgressSubscriber (devtools/ya/build/evlog/progress.py)
and DisplayMessageSubscriber (devtools/ya/build/ya_make.py); wired into
the configure-time subscription scope in build/ya_make.py when the agent
console is active.
"""

import logging
import threading
import time

import devtools.ya.core.event_handling as event_handling

logger = logging.getLogger(__name__)


class ConfigureSubscriber(event_handling.SubscriberSpecifiedTopics):
    """Frames the configure stage with started/progress/finished events.

    One configuration runs several ymake instances (host tools + target
    platforms) and the set is not monotonic: a late single-module run may
    start after every earlier instance has finished. The frame therefore
    opens on the first TStageStarted and closes only in on_unsubscribe(),
    when the configure subscription scope exits (see _build_graph_and_tests
    in devtools/ya/build/ya_make.py) — by construction no configure event
    can follow configure_finished. The scope unwinds through ``finally``,
    so an opened frame is closed even when the configuration fails hard.
    Module counters are aggregated across instances. Error display messages
    are only buffered on the console: on the normal path they reach the
    stream through the report pipeline, and the buffer is emitted only when
    the configuration fails hard.
    """

    topics = {
        "NEvent.TStageStarted",
        "NEvent.TConfModulesStat",
        "NEvent.TDisplayMessage",
    }

    _STAGE_NAME = 'ymake run'

    def __init__(self, console, progress_interval: float = 10) -> None:
        self._console = console
        self._progress_interval = progress_interval
        self._lock = threading.Lock()
        self._started_at: float | None = None
        self._finished = False
        # ymake_run_uid -> (done, total): TConfModulesStat counters are
        # cumulative per instance, so only the latest snapshot is kept.
        self._modules: dict = {}
        self._last_progress: tuple[int, int] | None = None
        self._last_progress_time: float | None = None

    def _action(self, event: dict) -> None:
        # Exception wall: a bug here must never break the configuration.
        try:
            self._process(event)
        except Exception:
            logger.exception("Failed to process a configure event for the agent console")

    def _process(self, event: dict) -> None:
        typename = event['_typename']
        if typename == 'NEvent.TDisplayMessage':
            if event.get('Type') == 'Error':
                self._console.buffer_configure_error(event)
            return

        to_emit = []
        with self._lock:
            if typename == 'NEvent.TStageStarted' and event.get('StageName') == self._STAGE_NAME:
                if self._started_at is None:
                    self._started_at = time.monotonic()
                    to_emit.append({'type': 'configure_started'})
            elif typename == 'NEvent.TConfModulesStat':
                progress = self._progress_event(event)
                if progress:
                    to_emit.append(progress)
        for queued in to_emit:
            self._console.emit(queued)

    def on_unsubscribe(self) -> None:
        # The event queue delivers events and unsubscribes under one lock,
        # so this is the definitive end of the stage: nothing arrives after.
        try:
            to_emit = []
            with self._lock:
                if self._started_at is None or self._finished:
                    return
                self._finished = True
                # Finalize the progress: the throttle may have swallowed
                # the definitive counters, flush them before the frame closes.
                final_progress = self._progress_snapshot()
                if final_progress:
                    to_emit.append(final_progress)
                to_emit.append(
                    {
                        'type': 'configure_finished',
                        'duration': round(time.monotonic() - self._started_at, 3),
                    }
                )
            for queued in to_emit:
                self._console.emit(queued)
        except Exception:
            logger.exception("Failed to close the configure frame for the agent console")

    def _progress_event(self, event: dict) -> dict | None:
        # Called under self._lock.
        uid = event.get('ymake_run_uid')
        if uid is None:
            return None
        self._modules[uid] = (event.get('Done') or 0, event.get('Total') or 0)
        now = time.monotonic()
        if self._last_progress_time is not None and now - self._last_progress_time < self._progress_interval:
            return None
        snapshot = self._progress_snapshot()
        if snapshot:
            self._last_progress_time = now
        return snapshot

    def _progress_snapshot(self) -> dict | None:
        # Called under self._lock: the aggregate counters deduped against the
        # last emitted pair; throttling is the caller's business.
        done = sum(instance_done for instance_done, _ in self._modules.values())
        total = sum(instance_total for _, instance_total in self._modules.values())
        if not done and not total:
            return None
        if (done, total) == self._last_progress:
            return None
        self._last_progress = (done, total)
        return {'type': 'configure_progress', 'done': done, 'total': total}
