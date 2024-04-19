import contextlib
import threading

import typing as tp  # noqa
from .base_subscriber import BaseSubscriber  # noqa


class EventQueue:
    def __init__(self, subscribers=None):
        self._subscribers = set(subscribers or set())  # type: set[BaseSubscriber]
        self._lock = threading.Lock()

    def subscribe(self, *subscribers):
        # type: (BaseSubscriber) -> None
        with self._lock:
            subscribers = set(subscribers)
            already_added = subscribers & self._subscribers

            if already_added:
                raise RuntimeError("You are trying to add subscribers that are already in the list: %s", already_added)

            self._subscribers |= subscribers
            for sub in subscribers:
                sub.on_subscribe()

    def unsubscribe(self, *subscribers):
        # type: (BaseSubscriber) -> None
        with self._lock:
            subscribers = set(subscribers)
            to_remove = subscribers & self._subscribers

            if to_remove != subscribers:
                raise RuntimeError(
                    "You are trying to remove subscribers, that are not in list of subscribers: %s",
                    subscribers - to_remove,
                )

            self._subscribers -= subscribers
            for sub in subscribers:
                sub.on_unsubscribe()

    def __call__(self, event):
        # type: (dict) -> None
        with self._lock:
            for sub in self._subscribers:
                sub.on_new_message(event)

    @contextlib.contextmanager
    def subscription_scope(self, *subscribers):
        # type: (BaseSubscriber) -> tp.Generator[EventQueue, tp.Any, tp.Any]
        self.subscribe(*subscribers)
        try:
            yield self
        finally:
            self.unsubscribe(*subscribers)
