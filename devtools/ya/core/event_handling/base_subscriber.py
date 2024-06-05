import six

from abc import ABCMeta
import logging
import threading

import exts.func as func

logger = logging.getLogger(__name__)


class BaseSubscriber(six.with_metaclass(ABCMeta, object)):
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


class SingletonSubscriber(six.with_metaclass(func.Singleton, object)):
    __lock = threading.Lock()

    def __lazy_init(self):
        if not hasattr(self, "_subscribers"):
            with self.__lock:
                if not hasattr(self, "_subscribers"):
                    self._subscribers = 0
                    self._lock = threading.Lock()

    def subscribe_to(self, q):
        self.__lazy_init()
        with self._lock:
            self._subscribers += 1
            if self._subscribers == 1:
                logger.debug("Subscribing {} to event_queue".format(self.__class__.__name__))
                q.subscribe(self)

    def unsubscribe_from(self, q):
        self.__lazy_init()
        with self._lock:
            self._subscribers -= 1
            if self._subscribers == 0:
                logger.debug("Unsubscribing {} from event_queue".format(self.__class__.__name__))
                q.unsubscribe(self)


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
