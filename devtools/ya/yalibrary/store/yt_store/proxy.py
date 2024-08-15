import functools
import logging

import yt.wrapper as yt
import yalibrary.store.yt_store.consts as consts

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


class YtClientProxy(object):
    """
    Check out README.md
    """

    def __init__(self, client, retryer):
        self._client = client
        self._retryer = retryer

    def _callable_wrapper(self, name, func):
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            attempt = 1

            while True:
                try:
                    return func(*args, **kwargs)
                except Exception as err:
                    self.handle_retry(err, attempt)
                    attempt += 1

        return wrapper

    def handle_retry(self, err, attempt):
        self._retryer.handle(err, attempt)

    def __getattr__(self, name):
        v = getattr(self._client, name)
        if callable(v):
            return self._callable_wrapper(name, v)
        return v

    @property
    def __dict__(self):
        return self._client.__dict__

    def __dir__(self):
        return self._client.__dir__()


class InterruptableYtClientProxy(YtClientProxy):
    MAX_NUM_OF_RETRIES = 5

    def __init__(self, client, retryer):
        super(InterruptableYtClientProxy, self).__init__(client, retryer)
        self._total_num_of_retries = 0
        self._last_error_logged = False

    @property
    def total_retries_exceeded(self):
        return self._total_num_of_retries >= self.MAX_NUM_OF_RETRIES

    def handle_retry(self, err, attempt):
        super(InterruptableYtClientProxy, self).handle_retry(err, attempt)
        self._total_num_of_retries += 1

        if not self._last_error_logged and self.total_retries_exceeded:
            logger.warning(
                "Disabling dist cache. Exceeded number of max retries (%s). Last caught error: %s",
                self.MAX_NUM_OF_RETRIES,
                err.__repr__(),
            )

            self._last_error_logged = True
