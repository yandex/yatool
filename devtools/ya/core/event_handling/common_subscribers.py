import json
import logging

from .base_subscriber import SubscriberExcludedTopics


logger = logging.getLogger(__name__)


class SubscriberLoggable(SubscriberExcludedTopics):
    # These topics are the most frequent.
    # Adding them to the logs and evlogs will greatly increase their size
    topics = {
        "NEvent.TNeedDirHint",
        "NEvent.TConfModulesStat",
        "NEvent.TFilesStat",
        "NEvent.TRenderModulesStat",
    }


class EventToLogSubscriber(SubscriberLoggable):
    def _action(self, event):
        # type: (dict) -> None
        event_sorted = json.dumps(event, sort_keys=True)
        logger.debug("Configure message %s", event_sorted)
