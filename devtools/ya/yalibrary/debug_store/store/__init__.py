import enum
from time import time

import typing as tp  # noqa: F401
from pathlib import Path

import logging


class Store:
    logger = logging.getLogger(__name__ + ':Store')
    VERSION = 1

    def __init__(self):
        self.data = {}

        self['__meta__'] = {'init_time': time(), 'version': self.VERSION}

    def log_item(self, key, value):
        # type: (str, tp.Any) -> None
        # TODO: Store callstack
        # TODO: Store exception (if exist)

        self._log_item(
            {
                'key': key,
                'value': value,
                'value_type': type(value).__name__,
            }
        )

    @classmethod
    def _log_item(cls, raw_data):
        cls.logger.debug("Log item: %s", raw_data)

    def __setitem__(self, key, value):
        self.log_item(key, value)
        assert isinstance(key, str), "Key must be string, not {}".format(type(key))

        if key in self.data and self.data[key] != value:
            self.logger.warning("Rewrite item (%s) in debug_store", key)
            self.logger.debug("Key (%s): `%s` -> `%s`", key, self.data[key], value)
        self.data[key] = value

    def __getitem__(self, key):
        assert isinstance(key, str), "Key must be string, not {}".format(type(key))
        return self.data[key]

    def __contains__(self, item):
        return item in self.data

    def __iter__(self):
        return iter(self.data)

    def __str__(self):
        return str(self.data)

    @classmethod
    def prepare_item_to_dump(cls, data):
        """Method to recursively clean items before dumps
        * Make path absolute
        * Translate Enum to json representation
        """
        if isinstance(data, Path):
            return str(data.absolute())
        if isinstance(data, dict):
            return {k: cls.prepare_item_to_dump(v) for k, v in data.items()}
        if isinstance(data, (list, tuple, set)):
            return tuple(cls.prepare_item_to_dump(item) for item in data)
        if isinstance(data, enum.Enum):
            return {
                '_type': type(data).__name__,
                '_module': type(data).__module__,
                '_value': data.value,
                '_name': data.name,
            }
        return data


class EvLogStore(Store):
    EVLOG_NAMESPACE = "dump_debug"
    evlog = None
    logger = logging.getLogger(__name__ + ':EvLogStore')

    @classmethod
    def evlog_write(cls, event, **items):
        if cls.evlog is None:
            return

        items = cls.prepare_item_to_dump(items)

        try:
            return cls.evlog.write(cls.EVLOG_NAMESPACE, event, **items)
        except Exception:
            cls.logger.exception("While store items, event")
            cls.logger.debug("Items: %s", items)

    @classmethod
    def _log_item(cls, raw_data):
        cls.evlog_write("log", **raw_data)
