import time
import logging
import functools

import yt.wrapper as yt
import yalibrary.store.yt_store.consts as consts

from yt.wrapper.dynamic_table_commands import (
    get_dynamic_table_retriable_errors as get_yt_retriable_errors,
)

logger = logging.getLogger(__name__)


def get_default_client(proxy, token):
    retries_policy = yt.default_config.retries_config(
        count=1,
        enable=False,
    )
    yt_fixed_config = {
        "proxy": {
            "request_timeout": consts.YT_CACHE_REQUEST_TIMEOUT_MS,
            "retries": retries_policy,
        },
        "dynamic_table_retries": retries_policy,
    }
    yt_config = yt.common.update(yt.default_config.get_config_from_env(), yt_fixed_config)
    return yt.YtClient(proxy=proxy, token=token, config=yt_config)


class RetryPolicy:
    SLEEP_S = 0.5

    def __init__(self, max_retries=5, on_error_callback=None):
        self.max_retries = max_retries
        self.disabled = False
        self._retryable_errors = get_yt_retriable_errors()
        self._on_error_callback = on_error_callback

    def execute(self, name, func):
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            for attempt_num in range(1, self.max_retries + 1):
                try:
                    return func(*args, **kwargs)
                except Exception as err:
                    if self.should_raise_error(err, attempt_num):
                        self.on_error(err)
                        raise err

                    time.sleep(self.SLEEP_S)

        return wrapper

    def on_error(self, err):
        if not self.disabled:
            if self._on_error_callback:
                self._on_error_callback(err)
            self.disabled = True

    def _is_yt_error_retryable(self, err):
        # type: (Exception) -> bool
        return isinstance(err, self._retryable_errors)

    def should_raise_error(self, err, attempt):
        # type: (Exception, int) -> bool
        return attempt >= self.max_retries or not self._is_yt_error_retryable(err)


class YtClientProxy:
    def __init__(self, client, retry_policy):
        self._client = client
        self._retry_policy = retry_policy
        self.is_disabled = False

    def __getattr__(self, name):
        try:
            v = getattr(self._client, name)
            if callable(v):
                return self._retry_policy.execute(name, v)
            return v
        except Exception:  # noqa
            self.is_disabled = True
            raise

    @property
    def __dict__(self):
        return self._client.__dict__

    def __dir__(self):
        return self._client.__dir__()
