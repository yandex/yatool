"""Mutable Python views over a packed C++ graph, built directly by the SAX loader.

Container identity is a native ID, independent of shared immutable value blocks.
Only modified containers allocate overlays. Views do not retain one another.
"""
from cpython.ref cimport PyObject, Py_INCREF

import copy
import operator
import threading
from collections import OrderedDict
try:
    from collections.abc import Mapping, MutableMapping, MutableSequence, KeysView, ItemsView, ValuesView
except ImportError:
    from collections import Mapping, MutableMapping, MutableSequence, KeysView, ItemsView, ValuesView

_MISSING = object()
_encoding = threading.local()


cdef class _GraphStorage:
    def __cinit__(self, bool keys, bool values):
        self.native = new TCompactStorage(keys, values)
        self.overrides = {}

    def __dealloc__(self):
        del self.native


cdef class _NativeRef:
    cdef TIndexType value

    def __init__(self, TIndexType value):
        self.value = value


class _MapChanges:
    __slots__ = ('replacements', 'removed', 'additions', 'version')

    def __init__(self):
        self.replacements = {}
        self.removed = set()
        self.additions = OrderedDict()
        self.version = 0


cdef class _View:
    cdef _GraphStorage _store
    cdef TIndexType _value

    def __cinit__(self, _GraphStorage store, TIndexType value):
        if store is None:
            raise TypeError('A compact view requires its storage')
        self._store = store
        self._value = value

    def __init__(self, *args):
        raise TypeError('Compact views are created by sjson.load(compact_paths=...)')

    @property
    def storage_stats(self):
        return {
            'native_bytes': self._store.native.NativeBytes(),
            'strings': self._store.native.StringCount(),
            'maps': self._store.native.MapCount(),
            'lists': self._store.native.ListCount(),
            'shapes': self._store.native.ShapeCount(),
            'values': self._store.native.ValueCount(),
            'shared_value_blocks': self._store.native.SharedBlocks(),
            'modified_containers': len(self._store.overrides),
        }

    def to_builtin(self):
        return _to_builtin(self, {})

    def __deepcopy__(self, memo):
        return _to_builtin(self, memo)

    def __call__(self, stream):
        active = getattr(_encoding, 'active', None)
        if active is None:
            active = _encoding.active = set()
        token = (id(self._store), self._value)
        if token in active:
            raise ValueError('Circular reference in compact graph')
        active.add(token)
        try:
            if self._store.overrides:
                EncodeCompact(stream, self._store.native[0], self._value, <PyObject*>self._store, _encoding_override)
            else:
                EncodeCompact(stream, self._store.native[0], self._value, <PyObject*>self._store, NULL)
        finally:
            active.remove(token)


cdef object _box(_GraphStorage store, TIndexType value):
    if (value & CompactKindMask) == List:
        return CompactList.__new__(CompactList, store, value)
    if (value & CompactKindMask) == Map:
        return CompactMap.__new__(CompactMap, store, value)
    return store.native.Scalar(value)


cdef object _wrap(_GraphStorage store, object value):
    if isinstance(value, _View) and (<_View>value)._store is store:
        return _NativeRef((<_View>value)._value)
    return value


cdef object _unwrap(_GraphStorage store, object value):
    if isinstance(value, _NativeRef):
        return _box(store, (<_NativeRef>value).value)
    return value


cdef object _map_get(CompactMap view, object key, object default):
    cdef TIndexType result
    if view._store.overrides:
        changes = view._store.overrides.get(view._value)
        if changes is not None:
            value = changes.additions.get(key, _MISSING)
            if value is not _MISSING:
                return _unwrap(view._store, value)
            if key in changes.removed:
                return default
            value = changes.replacements.get(key, _MISSING)
            if value is not _MISSING:
                return _unwrap(view._store, value)
    if view._store.native.MapGet(view._value, key, &result):
        return _box(view._store, result)
    return default


cdef class CompactMap(_View):
    def __len__(self):
        size = self._store.native.Size(self._value)
        if self._store.overrides:
            changes = self._store.overrides.get(self._value)
            if changes is not None:
                return size - len(changes.removed) + len(changes.additions)
        return size

    def __getitem__(self, key):
        result = _map_get(self, key, _MISSING)
        if result is _MISSING:
            raise KeyError(key)
        return result

    def get(self, key, default=None):
        return _map_get(self, key, default)

    def __contains__(self, key):
        cdef TIndexType value
        if self._store.overrides:
            changes = self._store.overrides.get(self._value)
            if changes is not None:
                if key in changes.additions:
                    return True
                if key in changes.removed:
                    return False
        return self._store.native.MapGet(self._value, key, &value)

    def __setitem__(self, key, value):
        cdef TIndexType old
        hash(key)
        changes = self._store.overrides.get(self._value)
        if changes is None:
            changes = self._store.overrides[self._value] = _MapChanges()
        value = _wrap(self._store, value)
        if key in changes.additions:
            changes.additions[key] = value
        elif key in changes.removed or not self._store.native.MapGet(self._value, key, &old):
            changes.additions[key] = value
            changes.version += 1
        else:
            changes.replacements[key] = value

    def __delitem__(self, key):
        if key not in self:
            raise KeyError(key)
        changes = self._store.overrides.get(self._value)
        if changes is None:
            changes = self._store.overrides[self._value] = _MapChanges()
        if key in changes.additions:
            del changes.additions[key]
        else:
            changes.removed.add(key)
            changes.replacements.pop(key, None)
        changes.version += 1

    def __iter__(self):
        cdef size_t index
        changes = self._store.overrides.get(self._value)
        version = changes.version if changes is not None else 0
        for index in range(self._store.native.Size(self._value)):
            current = self._store.overrides.get(self._value)
            if (current.version if current is not None else 0) != version:
                raise RuntimeError('dictionary changed during iteration')
            key = self._store.native.MapKey(self._value, index)
            if current is None or key not in current.removed:
                yield key
        if changes is not None:
            for key in changes.additions:
                if changes.version != version:
                    raise RuntimeError('dictionary changed during iteration')
                yield key
        current = self._store.overrides.get(self._value)
        if (current.version if current is not None else 0) != version:
            raise RuntimeError('dictionary changed during iteration')

    def keys(self):
        return KeysView(self)

    def items(self):
        return ItemsView(self)

    def values(self):
        return ValuesView(self)

    # six's Python 2 helpers call these methods directly rather than the ABC.
    def iterkeys(self):
        return iter(self)

    def iteritems(self):
        return iter(self.items())

    def itervalues(self):
        return iter(self.values())

    def viewkeys(self):
        return self.keys()

    def viewitems(self):
        return self.items()

    def viewvalues(self):
        return self.values()

    def copy(self):
        return dict(self.items())

    def __copy__(self):
        return self.copy()

    def update(self, *args, **kwargs):
        if len(args) > 1:
            raise TypeError('update accepts at most one positional argument')
        if args:
            source = args[0]
            items = ((key, source[key]) for key in source.keys()) if hasattr(source, 'keys') else source
            for key, value in items:
                self[key] = value
        for key, value in kwargs.items():
            self[key] = value

    def setdefault(self, key, default=None):
        value = _map_get(self, key, _MISSING)
        if value is _MISSING:
            self[key] = default
            return default
        return value

    def pop(self, key, *default):
        if len(default) > 1:
            raise TypeError('pop accepts at most two arguments')
        value = _map_get(self, key, _MISSING)
        if value is _MISSING:
            if default:
                return default[0]
            raise KeyError(key)
        del self[key]
        return value

    def popitem(self):
        if not self:
            raise KeyError('popitem(): dictionary is empty')
        key = list(self)[-1]
        return key, self.pop(key)

    def clear(self):
        for key in list(self):
            del self[key]

    def __repr__(self):
        return repr(self.to_builtin())

    def __richcmp__(self, other, int operation):
        if operation not in (2, 3) or not isinstance(other, Mapping):
            return NotImplemented
        if isinstance(other, _View) and (<_View>other)._store is self._store and (<_View>other)._value == self._value:
            equal = True
        else:
            equal = dict(self.items()) == dict(other.items())
        return equal if operation == 2 else not equal

    def __hash__(self):
        raise TypeError("unhashable type: 'CompactMap'")

    def __or__(self, other):
        if not isinstance(other, Mapping):
            return NotImplemented
        result = self.copy()
        result.update(other)
        return result

    def __ior__(self, other):
        self.update(other)
        return self


cdef list _list_override(CompactList view):
    result = view._store.overrides.get(view._value)
    if result is None:
        result = [_wrap(view._store, _box(view._store, view._store.native.Item(view._value, i)))
                  for i in range(view._store.native.Size(view._value))]
        view._store.overrides[view._value] = result
    return result


cdef class CompactList(_View):
    def __len__(self):
        if self._store.overrides:
            changed = self._store.overrides.get(self._value)
            if changed is not None:
                return len(changed)
        return self._store.native.Size(self._value)

    def __getitem__(self, index):
        cdef Py_ssize_t offset
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]
        try:
            offset = operator.index(index)
        except OverflowError:
            raise IndexError('list index out of range')
        size = len(self)
        if offset < 0:
            offset += size
        if offset < 0 or offset >= size:
            raise IndexError('list index out of range')
        if self._store.overrides:
            changed = self._store.overrides.get(self._value)
            if changed is not None:
                return _unwrap(self._store, changed[offset])
        return _box(self._store, self._store.native.Item(self._value, offset))

    def __iter__(self):
        cdef size_t index = 0
        cdef object changed
        while True:
            changed = self._store.overrides.get(self._value) if self._store.overrides else None
            if changed is not None:
                if index >= <size_t>len(changed):
                    return
                yield _unwrap(self._store, changed[index])
            else:
                if index >= self._store.native.Size(self._value):
                    return
                yield _box(self._store, self._store.native.Item(self._value, index))
            index += 1

    def __setitem__(self, index, value):
        if isinstance(index, slice):
            _list_override(self)[index] = [_wrap(self._store, item) for item in value]
        else:
            _list_override(self)[index] = _wrap(self._store, value)

    def __delitem__(self, index):
        del _list_override(self)[index]

    def insert(self, index, value):
        _list_override(self).insert(index, _wrap(self._store, value))

    def append(self, value):
        _list_override(self).append(_wrap(self._store, value))

    def extend(self, values):
        target = _list_override(self)
        if isinstance(values, CompactList) and (<CompactList>values)._store is self._store and (<CompactList>values)._value == self._value:
            target.extend(target)
        else:
            target.extend(_wrap(self._store, value) for value in values)

    def pop(self, index=-1):
        return _unwrap(self._store, _list_override(self).pop(index))

    def remove(self, value):
        del _list_override(self)[self.index(value)]

    def clear(self):
        self._store.overrides[self._value] = []

    def reverse(self):
        _list_override(self).reverse()

    def sort(self, *args, **kwargs):
        values = list(self)
        values.sort(*args, **kwargs)
        self._store.overrides[self._value] = [_wrap(self._store, value) for value in values]

    def index(self, value, *bounds):
        return list(self).index(value, *bounds)

    def count(self, value):
        return sum(item == value for item in self)

    def __contains__(self, value):
        return any(item == value for item in self)

    def copy(self):
        return list(self)

    def __copy__(self):
        return list(self)

    def __repr__(self):
        return repr(self.to_builtin())

    def __richcmp__(self, other, int operation):
        if not isinstance(other, (list, CompactList)):
            return NotImplemented
        if isinstance(other, _View) and (<_View>other)._store is self._store and (<_View>other)._value == self._value:
            return operation in (1, 2, 5)
        left, right = list(self), list(other)
        if operation == 0: return left < right
        if operation == 1: return left <= right
        if operation == 2: return left == right
        if operation == 3: return left != right
        if operation == 4: return left > right
        return left >= right

    def __hash__(self):
        raise TypeError("unhashable type: 'CompactList'")

    def __add__(self, other):
        if not isinstance(other, (list, CompactList)):
            return NotImplemented
        return list(self) + list(other)

    def __radd__(self, other):
        if not isinstance(other, list): return NotImplemented
        return other + list(self)

    def __iadd__(self, other):
        self.extend(other)
        return self

    def __mul__(self, count):
        return list(self) * count

    def __rmul__(self, count):
        return list(self) * count

    def __imul__(self, count):
        _list_override(self).__imul__(count)
        return self


def _to_builtin(value, memo):
    if not isinstance(value, _View):
        return copy.deepcopy(value, memo)
    key = (id((<_View>value)._store), (<_View>value)._value)
    if key in memo:
        return memo[key]
    result = {} if isinstance(value, CompactMap) else []
    memo[key] = result
    memo[id(value)] = result
    if isinstance(value, CompactMap):
        for name, child in value.items():
            result[name] = _to_builtin(child, memo)
    else:
        result.extend(_to_builtin(child, memo) for child in value)
    return result


cdef PyObject* _encoding_override(PyObject* owner, TIndexType value) except NULL:
    cdef _GraphStorage store = <_GraphStorage><object>owner
    if value in store.overrides:
        view = _box(store, value)
        result = dict(view.items()) if (value & CompactKindMask) == Map else list(view)
    else:
        result = None
    Py_INCREF(result)
    return <PyObject*>result


cdef PyObject* _compact_factory(PyObject* storage, TIndexType value) except NULL:
    result = _box(<_GraphStorage><object>storage, value)
    Py_INCREF(result)
    return <PyObject*>result


MutableMapping.register(CompactMap)
MutableSequence.register(CompactList)
