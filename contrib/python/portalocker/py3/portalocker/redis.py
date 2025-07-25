# pyright: reportUnknownMemberType=false
from __future__ import annotations

import _thread
import json
import logging
import random
import time
import typing

import redis

from . import exceptions, utils

logger = logging.getLogger(__name__)

DEFAULT_UNAVAILABLE_TIMEOUT = 1
DEFAULT_THREAD_SLEEP_TIME = 0.1


class PubSubWorkerThread(redis.client.PubSubWorkerThread):
    def run(self) -> None:
        try:
            super().run()
        except Exception:  # pragma: no cover
            _thread.interrupt_main()
            raise


class RedisLock(utils.LockBase):
    """
    An extremely reliable Redis lock based on pubsub with a keep-alive thread

    As opposed to most Redis locking systems based on key/value pairs,
    this locking method is based on the pubsub system. The big advantage is
    that if the connection gets killed due to network issues, crashing
    processes or otherwise, it will still immediately unlock instead of
    waiting for a lock timeout.

    To make sure both sides of the lock know about the connection state it is
    recommended to set the `health_check_interval` when creating the redis
    connection..

    Args:
        channel: the redis channel to use as locking key.
        connection: an optional redis connection if you already have one
        or if you need to specify the redis connection
        timeout: timeout when trying to acquire a lock
        check_interval: check interval while waiting
        fail_when_locked: after the initial lock failed, return an error
            or lock the file. This does not wait for the timeout.
        thread_sleep_time: sleep time between fetching messages from redis to
            prevent a busy/wait loop. In the case of lock conflicts this
            increases the time it takes to resolve the conflict. This should
            be smaller than the `check_interval` to be useful.
        unavailable_timeout: If the conflicting lock is properly connected
            this should never exceed twice your redis latency. Note that this
            will increase the wait time possibly beyond your `timeout` and is
            always executed if a conflict arises.
        redis_kwargs: The redis connection arguments if no connection is
            given. The `DEFAULT_REDIS_KWARGS` are used as default, if you want
            to override these you need to explicitly specify a value (e.g.
            `health_check_interval=0`)

    """

    redis_kwargs: dict[str, typing.Any]
    thread: PubSubWorkerThread | None
    channel: str
    timeout: float
    connection: redis.client.Redis[str] | None
    pubsub: redis.client.PubSub | None = None
    close_connection: bool

    DEFAULT_REDIS_KWARGS: typing.ClassVar[dict[str, typing.Any]] = dict(
        health_check_interval=10,
        decode_responses=True,
    )

    def __init__(
        self,
        channel: str,
        connection: redis.client.Redis[str] | None = None,
        timeout: float | None = None,
        check_interval: float | None = None,
        fail_when_locked: bool | None = False,
        thread_sleep_time: float = DEFAULT_THREAD_SLEEP_TIME,
        unavailable_timeout: float = DEFAULT_UNAVAILABLE_TIMEOUT,
        redis_kwargs: dict[str, typing.Any] | None = None,
    ) -> None:
        # We don't want to close connections given as an argument
        self.close_connection = not connection

        self.thread = None
        self.channel = channel
        self.connection = connection
        self.thread_sleep_time = thread_sleep_time
        self.unavailable_timeout = unavailable_timeout
        self.redis_kwargs = redis_kwargs or dict()

        for key, value in self.DEFAULT_REDIS_KWARGS.items():
            self.redis_kwargs.setdefault(key, value)

        super().__init__(
            timeout=timeout,
            check_interval=check_interval,
            fail_when_locked=fail_when_locked,
        )

    def get_connection(self) -> redis.client.Redis[str]:
        if not self.connection:
            self.connection = redis.client.Redis(**self.redis_kwargs)

        return self.connection

    def channel_handler(self, message: dict[str, str]) -> None:
        if message.get('type') != 'message':  # pragma: no cover
            return

        raw_data = message.get('data')
        if not raw_data:
            return

        try:
            data = json.loads(raw_data)
        except TypeError:  # pragma: no cover
            logger.debug('TypeError while parsing: %r', message)
            return

        assert self.connection is not None
        self.connection.publish(data['response_channel'], str(time.time()))

    @property
    def client_name(self) -> str:
        return f'{self.channel}-lock'

    def _timeout_generator(
        self, timeout: float | None, check_interval: float | None
    ) -> typing.Iterator[int]:
        if timeout is None:
            timeout = 0.0
        if check_interval is None:
            check_interval = self.thread_sleep_time
        deadline = time.monotonic() + timeout
        first = True
        while first or time.monotonic() < deadline:
            first = False
            effective_interval = (
                check_interval
                if check_interval > 0
                else self.thread_sleep_time
            )
            sleep_time = effective_interval * (0.5 + random.random())
            time.sleep(sleep_time)
            yield 0

    def acquire(  # type: ignore[override]
        self,
        timeout: float | None = None,
        check_interval: float | None = None,
        fail_when_locked: bool | None = None,
    ) -> RedisLock:
        timeout = utils.coalesce(timeout, self.timeout, 0.0)
        check_interval = utils.coalesce(
            check_interval,
            self.check_interval,
            0.0,
        )
        fail_when_locked = utils.coalesce(
            fail_when_locked,
            self.fail_when_locked,
        )

        assert not self.pubsub, 'This lock is already active'
        connection = self.get_connection()

        timeout_generator = self._timeout_generator(timeout, check_interval)
        for _ in timeout_generator:  # pragma: no branch
            subscribers = connection.pubsub_numsub(self.channel)[0][1]

            if subscribers:
                logger.debug(
                    'Found %d lock subscribers for %s',
                    subscribers,
                    self.channel,
                )

                if self.check_or_kill_lock(
                    connection,
                    self.unavailable_timeout,
                ):  # pragma: no branch
                    continue
                else:  # pragma: no cover
                    subscribers = 0

            # Note: this should not be changed to an elif because the if
            # above can still end up here
            if not subscribers:
                connection.client_setname(self.client_name)
                self.pubsub = connection.pubsub()
                self.pubsub.subscribe(**{self.channel: self.channel_handler})
                self.thread = PubSubWorkerThread(
                    self.pubsub,
                    sleep_time=self.thread_sleep_time,
                )
                self.thread.start()
                time.sleep(0.01)
                subscribers = connection.pubsub_numsub(self.channel)[0][1]
                if subscribers == 1:  # pragma: no branch
                    return self
                else:  # pragma: no cover
                    # Race condition, let's try again
                    self.release()

            if fail_when_locked:  # pragma: no cover
                raise exceptions.AlreadyLocked()

        raise exceptions.AlreadyLocked()

    def check_or_kill_lock(
        self,
        connection: redis.client.Redis[str],
        timeout: float,
    ) -> bool | None:
        # Random channel name to get messages back from the lock
        response_channel = f'{self.channel}-{random.random()}'

        pubsub = connection.pubsub()
        pubsub.subscribe(response_channel)
        connection.publish(
            self.channel,
            json.dumps(
                dict(
                    response_channel=response_channel,
                    message='ping',
                ),
            ),
        )

        check_interval = min(self.thread_sleep_time, timeout / 10)
        for _ in self._timeout_generator(
            timeout,
            check_interval,
        ):  # pragma: no branch
            if pubsub.get_message(timeout=check_interval):
                pubsub.close()
                return True

        for client_ in connection.client_list('pubsub'):  # pragma: no cover
            if client_.get('name') == self.client_name:
                logger.warning('Killing unavailable redis client: %r', client_)
                connection.client_kill_filter(  # pyright: ignore
                    client_.get('id'),
                )
        return None

    def release(self) -> None:
        if self.thread:  # pragma: no branch
            self.thread.stop()
            self.thread.join()
            self.thread = None
            time.sleep(0.01)

        if self.pubsub:  # pragma: no branch
            self.pubsub.unsubscribe(self.channel)
            self.pubsub.close()
            self.pubsub = None

    def __del__(self) -> None:
        self.release()
