import abc
import logging
import time

import yt.wrapper as yt

logger = logging.getLogger(__name__)


class BaseOnRetryHandler:
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def handle(self, err, attempt):
        # type: (Exception, int) -> None
        pass


class DefaultYtClientRetryerHandler(BaseOnRetryHandler):
    REQUEST_RETRIES = 3
    BACKOFF_MS = 500
    BACKOFF_FN = time.sleep

    def __init__(self):
        self._retryable_errors = yt.http_helpers.get_retriable_errors()

    def handle(self, err, attempt):
        # type: (Exception, int) -> None

        should_raise_error = self._attempts_exceeded(attempt) or not self._is_yt_error_retryable(err)
        if should_raise_error:
            raise  # noqa: PLE0704,PLE704

        self._backoff_action(attempt)

    def _is_yt_error_retryable(self, err):
        # type: (Exception) -> bool
        return isinstance(err, self._retryable_errors)

    @classmethod
    def _attempts_exceeded(cls, attempt):
        # type: (int) -> bool
        return attempt > cls.REQUEST_RETRIES

    @classmethod
    def _backoff_action(cls, attempt):
        # type: (int) -> None

        sleep_time = cls.BACKOFF_MS / 1000.0
        cls.BACKOFF_FN(sleep_time)
