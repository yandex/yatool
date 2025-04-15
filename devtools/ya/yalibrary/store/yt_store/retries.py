import logging
import functools
import threading
from collections.abc import Callable

import yt.wrapper as yt
import yalibrary.store.yt_store.consts as consts

from library.python.retry import RetryConf, retry_call
from yt.wrapper.dynamic_table_commands import (
    get_dynamic_table_retriable_errors as get_yt_retriable_errors,
)

logger = logging.getLogger(__name__)


def get_default_client(proxy: str, token: str) -> yt.YtClient:
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
    MIN_SLEEP = 0.3
    MAX_SLEEP = 10

    def __init__(
        self, max_retries: int = 5, on_error_callback: Callable | None = None, retry_time_limit: float | None = None
    ):
        self.disabled = False
        self._lock = threading.Lock()
        self._on_error_callback = on_error_callback
        retry_conf = (
            RetryConf()
            .on(*get_yt_retriable_errors())
            .waiting(delay=self.MIN_SLEEP, backoff=1.3, jitter=0.2, limit=self.MAX_SLEEP)
        )
        if retry_time_limit:
            self._retry_conf = retry_conf.upto(retry_time_limit)
        elif max_retries:
            self._retry_conf = retry_conf.upto_retries(max_retries)
        else:
            raise RuntimeError("Either max_retries or max_retry_timeout must be specified")

    def wrap(self, func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            try:
                return retry_call(func, f_args=args, f_kwargs=kwargs, conf=self._retry_conf)
            except Exception as err:
                self.on_error(err)
                raise

        return wrapper

    def on_error(self, err: Exception) -> None:
        with self._lock:
            if self.disabled:
                return
            self.disabled = True
            if self._on_error_callback:
                self._on_error_callback(err)


class YtClientProxy:
    def __init__(self, client: yt.YtClient, retry_policy: RetryPolicy):
        self._client = client
        self._retry_policy = retry_policy

    def __getattr__(self, name: str):
        v = getattr(self._client, name)
        if callable(v):
            return self._retry_policy.wrap(v)
        return v

    @property
    def __dict__(self):
        return self._client.__dict__

    def __dir__(self):
        return self._client.__dir__()
