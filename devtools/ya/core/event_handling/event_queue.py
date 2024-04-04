from .base_subscriber import BaseSubscriber  # noqa


class EventQueue:
    def __init__(self, subscribers=None):
        self._subscribers = set(subscribers or set())  # type: set[BaseSubscriber]

    def subscribe(self, *subscribers):
        # type: (list[BaseSubscriber]) -> None
        subscribers = set(subscribers)
        already_added = subscribers & self._subscribers

        if already_added:
            raise RuntimeError("You are trying to add subscribers that are already in the list: %s", already_added)

        self._subscribers |= subscribers

    def unsubscribe(self, *subscribers):
        # type: (list[BaseSubscriber]) -> None
        subscribers = set(subscribers)
        to_remove = subscribers & self._subscribers

        if to_remove != subscribers:
            raise RuntimeError(
                "You are trying to remove subscribers, that are not in list of subscribers: %s", subscribers - to_remove
            )

        self._subscribers -= subscribers

    def __call__(self, event):
        # type: (dict) -> None
        for sub in self._subscribers:
            sub.on_new_message(event)
