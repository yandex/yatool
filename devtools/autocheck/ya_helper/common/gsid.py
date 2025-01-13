from collections import defaultdict

from .logger_count import LoggerCounter


class GSIDParts(LoggerCounter):
    VALUE_SPLITTER = ":"
    ITEM_SPLITTER = " "

    @classmethod
    def create_from_environ(cls):
        # type: () -> GSIDParts
        import os

        return GSIDParts(os.environ.get('GSID'))

    def __init__(
        self,
        raw_gsid=None,  # type: str
        _parts=None,  # type: dict
    ):
        self.original_parts = defaultdict(list)
        self._raw_gsid = raw_gsid

        if self._raw_gsid:
            for item in self._raw_gsid.split(self.ITEM_SPLITTER):
                if not item:
                    continue

                if self.VALUE_SPLITTER not in item:
                    self.logger.warning("Not found splitter in part of raw GSID: `%s`", item)
                    continue

                k, v = item.split(":", 1)
                self.original_parts[k].append(v)
        else:
            self._raw_gsid = ""

        self.updated_parts = _parts or {}

    def copy(self):
        return GSIDParts(raw_gsid=self._raw_gsid, _parts=self.updated_parts.copy())

    def _check_key(self, key):
        if key != key.upper():
            raise KeyError("Allowed only key names on upper case", key)

    def __setitem__(self, key, value, force=True):
        value = str(value)

        self._check_key(key)

        if key in self.original_parts:
            if value in self.original_parts[key]:
                return  # do nothing

            log_err = "Replace key `{}` with different value: `{}` (expected one of `{}`)".format(
                key, value, self.original_parts[key]
            )

            if not force:
                # Value not in original parts
                raise ValueError(log_err, self, key, value, self.original_parts[key])
            else:
                self.logger.warning(log_err)

        if key in self.updated_parts:
            if self[key] != value:
                log_err = "Replace key `{}` with different value: `{}` (expected `{}`)".format(key, value, self[key])
                if not force:
                    raise ValueError(log_err, self, key, value, self[key])
                else:
                    self.logger.warning(log_err)

        if self.ITEM_SPLITTER in value:
            raise ValueError(
                "Symbol `{}` forbidden in value `{}` by key `{}`".format(self.ITEM_SPLITTER, value, key),
                self,
                key,
                value,
            )

        self.updated_parts[key] = value

    def __contains__(self, key):
        self._check_key(key)

        return (key in self.original_parts) or (key in self.updated_parts)

    def __getitem__(self, key):
        self._check_key(key)

        if key in self.updated_parts:
            return self.updated_parts[key]

        if key in self.original_parts:
            return self.original_parts[key][-1]  # return last value

        raise KeyError(key)

    def __str__(self):
        return (
            self._raw_gsid
            + " "
            + self.ITEM_SPLITTER.join(
                "{}{}{}".format(k, self.VALUE_SPLITTER, self[k]) for k in sorted(self.updated_parts.keys())
            )
        )
