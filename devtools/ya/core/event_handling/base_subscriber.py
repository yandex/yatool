from abc import ABCMeta
import logging


logger = logging.getLogger(__name__)


class BaseSubscriber(object):
    __metaclass__ = ABCMeta

    def __init__(self):
        # type: () -> None
        pass

    def _filter_event(self, event):
        # type: (dict) -> bool
        raise NotImplementedError()

    def _action(self, event):
        # type: (dict) -> None
        raise NotImplementedError()

    def on_new_message(self, msg):
        # type: (dict) -> None
        if self._filter_event(msg):
            self._action(msg)

    def on_subscribe(self):
        # type: () -> None
        pass

    def on_unsubscribe(self):
        # type: () -> None
        pass


class SubscriberSpecifiedTopics(BaseSubscriber):
    topics = set()

    def _filter_event(self, event):
        # type: (dict) -> bool
        return event['_typename'] in self.topics


class SubscriberExcludedTopics(BaseSubscriber):
    topics = set()

    def _filter_event(self, event):
        # type: (dict) -> bool
        return event['_typename'] not in self.topics
