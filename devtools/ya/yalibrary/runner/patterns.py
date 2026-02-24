import logging
import re
from six.moves import collections_abc

import six

SUBST_PATTERN = re.compile(r'\$\((.*?)\)')


class Patterns(object):
    def __init__(self, parent=None):
        self._parent = parent
        self._map = {}

    def __setitem__(self, key, value):
        assert key not in self._map
        assert isinstance(value, six.string_types)
        self._map[key] = value

    def sub(self):
        return Patterns(self)

    def source_root(self):
        return self['SOURCE_ROOT']  # XXX

    def build_root(self):
        return self['BUILD_ROOT']  # XXX

    def tool_root(self):
        return self['TOOL_ROOT']  # XXX

    def resource_root(self):
        return self['RESOURCE_ROOT']  # XXX

    def __getitem__(self, key):
        try:
            return self._map[key]
        except KeyError:
            if self._parent is not None:
                return self._parent[key]
            else:
                raise

    def __contains__(self, key):
        return key in self._map

    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    @staticmethod
    def _iter(obj):
        try:
            if obj is None:
                return
            elif isinstance(obj, six.string_types):  # In python2: str & bytes, in py3 only str
                for x in SUBST_PATTERN.finditer(obj):
                    yield x.group(1)
            elif isinstance(obj, dict):
                for v in six.itervalues(obj):
                    for x in Patterns._iter(v):
                        yield x
            elif isinstance(obj, six.binary_type):
                # This will fail only on python3
                raise TypeError(
                    "Can't process pattern for binary string `{!r}`, check logs and convert it into str".format(obj)
                )
            elif isinstance(obj, collections_abc.Iterable):
                for v in obj:
                    for x in Patterns._iter(v):
                        yield x

            else:
                raise TypeError("Unknown value type to find pattern `{!r}`: `{!r}`".format(type(obj), obj))
        except Exception:
            logging.warning("Patterns._iter stack: (%r) `%r`", type(obj), obj)
            raise

    def unresolved(self, obj):
        seen = set()

        for x in self._iter(obj):
            seen.add(x)

        for x in seen:
            try:
                self[x]
            except KeyError:
                yield x

    def fix(self, obj):
        return self.fill(obj)

    def fill(self, obj):
        try:
            if obj is None:
                return obj
            if isinstance(obj, six.string_types):  # py2: str & bytes, py3: str
                return SUBST_PATTERN.sub(lambda x: self.get(x.group(1), x.group(0)), obj)
            if isinstance(obj, dict):
                return {k: self.fill(v) for k, v in six.iteritems(obj)}
            if isinstance(obj, six.binary_type):
                # This will fail only on python3
                raise TypeError("Can't fill binary string `{!r}`, check logs and convert it into str".format(obj))
            if isinstance(obj, collections_abc.Iterable):
                return [self.fill(x) for x in obj]

            raise TypeError("Unknown value type to fill `{!r}`: {!r}".format(type(obj), obj))
        except Exception:
            logging.warning("Patterns.fill stack: `%r`", obj)
            raise
