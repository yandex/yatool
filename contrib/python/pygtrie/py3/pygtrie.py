# -*- coding: utf-8 -*-
"""Pure Python implementation of a trie data structure.

`Trie data structure <http://en.wikipedia.org/wiki/Trie>`_, also known as radix
or prefix tree, is a tree associating keys to values where all the descendants
of a node have a common prefix (associated with that node).

The trie module contains :class:`Trie`, :class:`CharTrie` and
:class:`StringTrie` classes each implementing a mutable mapping interface,
i.e. :class:`dict` interface.  As such, in most circumstances, :class:`Trie`
could be used as a drop-in replacement for a :class:`dict`, but the prefix
nature of the data structure is trie’s real strength.

The module also contains :class:`PrefixSet` class which uses a trie to store
a set of prefixes such that a key is contained in the set if it, or any of its
prefixes, is stored in the set.

Features
--------

- A full mutable mapping implementation.

- Supports iterating over as well as deleting a branch of a trie (i.e. subtrie)

- Supports prefix checking as well as shortest and longest prefix look-up.

- Extensible for any kind of user-defined keys.

- A PrefixSet supports “all keys starting with given prefix” logic.

- Can store any value including None.
"""

__author__ = 'Michał Nazarewicz <mina86@mina86.com>'
__copyright__ = ('Copyright 2014-2017 Google LLC',
                 'Copyright 2018-2026 Michał Nazarewicz <mina86@mina86.com>')
__version__ = '2.6.1'


import copy as _copy
import collections.abc as _abc
import warnings as _warnings
import types as _types
import typing as _t


K = _t.TypeVar('K')
V = _t.TypeVar('V')
K_contra = _t.TypeVar('K_contra', contravariant=True)
V_contra = _t.TypeVar('V_contra', contravariant=True)
V_co = _t.TypeVar('V_co', covariant=True)
S = _t.TypeVar('S')
T = _t.TypeVar('T')


class _SupportsKeysAndGetItem(_t.Protocol[K, V_co]):
    def keys(self) -> _t.Iterable[K]: ...
    def __getitem__(self, key: K, /) -> V_co: ...

_Other = _SupportsKeysAndGetItem[K, V] | _t.Iterable[tuple[K, V]]


class _MakeCopy(_t.Protocol):
    """A callable which copies (or otherwise maps) a value to itself.

    This is used both to copy trie’s steps and trie’s values, hence the argument
    and return type are described by a method-scoped type variable rather than
    the module-level ``V`` so a single ``_MakeCopy`` object can be called with
    different (unrelated) types over the course of a copy.
    """

    def __call__(self, value: T) -> T: ...


class ShortKeyError(KeyError):
    """Raised when given key is a prefix of an existing longer key
    but does not have a value associated with itself."""


class _NoCopy:
    """Object which returns itself when copying."""
    __slots__ = ()
    def __copy__(self) -> _t.Self:
        return self
    def __deepcopy__(self, memo: _t.Any) -> _t.Self:
        return self


# Sentinel used as default value in function arguments.
_Sentinel = _t.NewType('_Sentinel', _NoCopy)
_SENTINEL = _Sentinel(_NoCopy())

def _is_not_sentinel(value: T | _Sentinel) -> _t.TypeGuard[T]:
    return value is not _SENTINEL

# Sentinel indicating node has no value.
_NoValue = _t.NewType('_NoValue', _NoCopy)
_NOVAL = _NoValue(_NoCopy())

def _is_value(value: V | _NoValue) -> _t.TypeGuard[V]:
    return value is not _NOVAL


class _FalsyIterator(_NoCopy):
    """An empty iterator which is in addition falsy."""
    __slots__ = ()

    __instance: _t.Self

    def __new__(cls) -> _t.Self:
        return cls.__instance

    def __bool__(self) -> _t.Literal[False]:
        return False
    def __iter__(self) -> _t.Self:
        return self
    def __next__(self) -> _t.Never:
        raise StopIteration

_FalsyIterator._FalsyIterator__instance = (  # type: ignore[attr-defined]  # pylint: disable=protected-access
    object.__new__(_FalsyIterator))


class _AnyChildren(_t.Protocol[S, V]):
    """Protocol for node’s children.  Covers cases with no children and with
    children."""

    def __bool__(self) -> bool:
        """Returns whether there are any children."""

    def __len__(self) -> int:
        """Returns number of children."""

    def items(self) -> _t.Iterable[tuple[S, '_Node[S, V]']]:
        """Iterates over all children as ``(step, node)`` tuples."""

    def sorted_items(self) -> _t.Iterable[tuple[S, '_Node[S, V]']]:
        """Iterates over all children as ``(step, node)`` tuples in sorted
        order."""

    def get(self, step: S) -> _t.Union['_Node[S, V]', None]:
        """Returns child at given step, or ``None`` if missing."""

    def add(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        """Adds a child at given step; returns the new node.

        ``parent`` must be the ``_Node`` object which owns this object.  In some
        situations, adding a child will change the ``parent.children`` object.
        """

    def require(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        """Adds a child at given step if missing; returns existing or the new
        node.

        ``parent`` must be the ``_Node`` object which owns this object.  In some
        situations, adding a child will change the ``parent.children`` object.
        """

    def merge(self,
              other: '_AnyChildren[S, V]',
              queue: list[tuple['_Node[S, V]', '_Node[S, V]']],
              ) -> '_AnyChildren[S, V]':
        """Moves nodes from ``other`` into this object and returns new container
        with all the children.

        The correct usage of the method is::

            parent.children = parent.children.merge(other.children, queue)
            other.children = _NoChildren()
        """

    def copy(self,
             make_copy: _MakeCopy,
             queue: list[_t.Iterable['_Node[S, V]']]) -> _t.Self:
        """Recursively copies the current object.  ``make_copy`` is used to copy
        the step and value objects."""

    def pick(self) -> tuple[S, '_Node[S, V]']:
        """Picks arbitrary child.

        Not implemented in :class:`_NoChildren`.
        """

    def delete(self, parent: '_Node[S, V]', step: S) -> None:
        """Deletes specified child.

        Not implemented in :class:`_NoChildren`.  Unspecified behaviour if
        ``step`` does not exist in the container.
        """


class _NoChildren(_AnyChildren[S, V], _NoCopy):
    """Collection representing lack of any children."""
    __slots__ = ()

    __instance: _t.Self

    def __new__(cls) -> _t.Self:
        return cls.__instance

    def __bool__(self) -> _t.Literal[False]:
        return False
    def __len__(self) -> _t.Literal[0]:
        return 0

    def items(self) -> tuple[()]:
        return ()
    sorted_items = items

    def get(self, step: S) -> None:
        return None

    def add(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        node: _Node[S, V] = _Node()
        parent.children = _OneChild(step, node)
        return node

    require = add

    def merge(self,
              other: '_AnyChildren[S, V]',
              queue: list[tuple['_Node[S, V]', '_Node[S, V]']],
              ) -> '_AnyChildren[S, V]':
        return other

    def copy(self,
             make_copy: _MakeCopy,
             queue: list[_t.Iterable['_Node[S, V]']]) -> _t.Self:
        return self

    def pick(self) -> tuple[S, '_Node[S, V]']:
        raise NotImplementedError()

    def delete(self, parent: '_Node[S, V]', step: S) -> None:
        raise NotImplementedError()

_NoChildren._NoChildren__instance = (  # type: ignore[attr-defined]  # pylint: disable=protected-access
    object.__new__(_NoChildren))


class _OneChild(_AnyChildren[S, V]):
    """Children collection representing a single child."""
    __slots__ = ('step', 'node')

    step: S
    node: '_Node[S, V]'

    def __init__(self, step: S, node: '_Node[S, V]') -> None:
        self.step = step
        self.node = node

    def __bool__(self) -> _t.Literal[True]:
        return True
    def __len__(self) -> _t.Literal[1]:
        return 1

    def items(self) -> tuple[tuple[S, '_Node[S, V]']]:
        return ((self.step, self.node),)
    sorted_items = items

    def pick(self) -> tuple[S, '_Node[S, V]']:
        return (self.step, self.node)

    def get(self, step: S) -> _t.Union['_Node[S, V]', None]:
        return self.node if step == self.step else None

    def add(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        node: _Node[S, V] = _Node()
        parent.children = _Children({
            self.step: self.node,
            step: node
        })
        return node

    def require(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        return self.node if self.step == step else self.add(parent, step)

    def merge(self,
              other: '_AnyChildren[S, V]',
              queue: list[tuple['_Node[S, V]', '_Node[S, V]']],
              ) -> '_AnyChildren[S, V]':
        # pylint: disable=unidiomatic-typecheck
        if type(other) is _OneChild and other.step == self.step:
            queue.append((self.node, other.node))
            return self
        elif other:
            children = _Children({ self.step: self.node })
            children.merge(other, queue)
            return children
        else:
            return self

    def delete(self, parent: '_Node[S, V]', step: S) -> None:
        parent.children = _NoChildren()

    def copy(self,
             make_copy: _MakeCopy,
             queue: list[_t.Iterable['_Node[S, V]']]) -> '_OneChild[S, V]':
        cpy = _OneChild(make_copy(self.step), self.node.shallow_copy(make_copy))
        queue.append((cpy.node,))
        return cpy


class _Children(_AnyChildren[S, V]):
    """Children collection representing more than one child."""

    __slots__ = ('_nodes',)
    _nodes: dict[S, '_Node[S, V]']

    def __init__(self, nodes: dict[S, '_Node[S, V]']) -> None:
        self._nodes = nodes

    def __bool__(self) -> _t.Literal[True]:
        return True
    def __len__(self) -> int:
        return len(self._nodes)

    def items(self) -> _t.Iterable[tuple[S, '_Node[S, V]']]:
        return self._nodes.items()

    def sorted_items(self) -> list[tuple[S, '_Node[S, V]']]:
        return sorted(self._nodes.items())

    def pick(self) -> tuple[S, '_Node[S, V]']:
        return next(iter(self._nodes.items()))

    def get(self, step: S) -> _t.Union['_Node[S, V]', None]:
        return self._nodes.get(step)

    def __getitem__(self, step: S) -> '_Node[S, V]':
        return self._nodes[step]

    def add(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        node: '_Node[S, V]' = _Node()
        self._nodes[step] = node
        return node

    def require(self, parent: '_Node[S, V]', step: S) -> '_Node[S, V]':
        return self._nodes.setdefault(step, _Node())

    def merge(self,
              other: '_AnyChildren[S, V]',
              queue: list[tuple['_Node[S, V]', '_Node[S, V]']]) -> _t.Self:
        for step, other_node in other.items():
            node = self._nodes.setdefault(step, other_node)
            if node is not other_node:
                queue.append((node, other_node))
        return self

    def delete(self, parent: '_Node[S, V]', step: S) -> None:
        del self._nodes[step]
        if len(self) == 1:
            parent.children = _OneChild(*self._nodes.popitem())

    def copy(self,
             make_copy: _MakeCopy,
             queue: list[_t.Iterable['_Node[S, V]']]) -> '_Children[S, V]':
        nodes = {make_copy(step): node.shallow_copy(make_copy)
                 for step, node in self.items()}
        queue.append(nodes.values())
        return _Children(nodes)


class NodeFactory(_t.Protocol[K_contra, V_contra, S, T]):
    """A node factory used when traversing a trie.  For more details, see
    :func:`Trie.traverse`."""

    @_t.overload
    def __call__(self,
                 key_from_path: _t.Callable[[_t.Iterable[S]], K_contra],
                 path: _t.Sequence[S],
                 children: _t.Iterable[T],
                 /) -> T: ...
    @_t.overload
    def __call__(self,
                 key_from_path: _t.Callable[[_t.Iterable[S]], K_contra],
                 path: _t.Sequence[S],
                 children: _t.Iterable[T],
                 value: V_contra,
                 /) -> T: ...
    def __call__(self,
                 key_from_path: _t.Callable[[_t.Iterable[S]], K_contra],
                 path: _t.Sequence[S],
                 children: _t.Iterable[T],
                 value: V_contra=_SENTINEL,  # type: ignore[assignment]
                 /) -> T:
        """Processes and transforms a node of a trie.  For more details, see
        :func:`Trie.traverse`.

        Args:
            key_from_path: A function converting ``path`` to a key as used in
                the trie.
            path: Path to the node being processed.
            children: A lazy iterator over children of the node.  The iterator
                is falsy if the node has no children; it’s truthy otherwise.
                The items of the iterator are values returned by calls to the
                node factory on corresponding child.
            value: If provided, a value assigned to the node.
        Returns:
            A value which is passed to parents through ``children`` iterator and
            eventually returned by the :func:`Trie.traverse` method.
        """


class _Node(_t.Generic[S, V]):
    """A single node of a trie.

    Stores value associated with the node and dictionary of children.
    """
    __slots__ = ('children', 'value')

    children: _AnyChildren[S, V]
    value: V | _NoValue

    def __init__(self) -> None:
        self.children = _NoChildren()
        self.value = _NOVAL

    def merge(self, other: '_Node[S, V]', overwrite: bool) -> None:
        """Move children from other node into this one.

        Args:
            other: Other node to move children and value from.
            overwrite: Whether to overwrite existing node values.
        """
        queue: list[tuple[_Node[S, V], _Node[S, V]]] = [(self, other)]
        while queue:
            lhs, rhs = queue.pop()
            if lhs.value is _NOVAL or (overwrite and rhs.value is not _NOVAL):
                lhs.value = rhs.value
            lhs.children = lhs.children.merge(rhs.children, queue)
            rhs.children = _NoChildren()

    def iterate(
            self,
            path: list[S],
            shallow: bool,
            items: _t.Callable[[_AnyChildren[S, V]],
                               _t.Iterable[tuple[S, '_Node[S, V]']]],
    ) -> _t.Iterator[tuple[list[S], V]]:
        """Yields all the nodes with values associated to them in the trie.

        Args:
            path: Path leading to this node.  Used to construct the key when
                returning value of this node and as a prefix for children.
            shallow: Perform a shallow traversal, i.e. do not yield nodes if
                their prefix has been yielded.
            items: A callable which takes ``node.children`` as a sole argument
                and returns an iterable of children as ``(step, node)`` pairs.
                It would typically call ``items`` or ``sorted_items`` method on
                the argument depending on whether sorted output is desired.

        Yields:
            ``(path, value)`` tuples.
        """
        # Use iterative function with stack on the heap so we don’t hit Python’s
        # recursion depth limits.
        node = self
        stack = []
        while True:
            if _is_value(value := node.value):
                yield (path, value)

            if (not shallow or node.value is _NOVAL) and node.children:
                stack.append(iter(items(node.children)))
                # None value will be overridden by `path[-1] = step` below; it’s
                # alright to temporarily append None.
                path.append(None)  # type: ignore[arg-type]

            while True:
                try:
                    step, node = next(stack[-1])
                    path[-1] = step
                    break
                except StopIteration:
                    stack.pop()
                    path.pop()
                except IndexError:
                    return

    def traverse(self,
                 node_factory: NodeFactory[K, V, S, T],
                 key_from_path: _t.Callable[[_t.Iterable[S]], K],
                 path: list[S],
                 items: _t.Callable[[_AnyChildren[S, V]],
                                    _t.Iterable[tuple[S, '_Node[S, V]']]]) -> T:
        """Traverses the node and returns another type of node from factory.

        Args:
            node_factory: Callable to construct return value.
            key_from_path: Callable to convert node path to a key.
            path: Current path for this node.
            items: A callable which takes ``node.children`` as a sole argument
                and returns an iterable of children as ``(step, node)`` pairs.
                It would typically call ``items`` or ``sorted_items`` method on
                the argument depending on whether sorted output is desired.

        Returns:
            The object constructed by calling ``node_factory(key_from_path,
            path, children, value=...)``, where ``children`` are constructed by
            ``node_factory`` from the children of this node.
        """
        children: _t.Iterable[T]
        if self.children:
            children = (
                node.traverse(node_factory, key_from_path, path + [step], items)
                for step, node in items(self.children))
        else:
            children = _FalsyIterator()

        value = self.value
        value_maybe = () if value is _NOVAL else (value,)

        return node_factory(key_from_path, tuple(path), children, *value_maybe)

    def equals(self, other: '_Node[S, V]') -> bool:
        """Returns whether this and other node are recursively equal."""
        # Like iterate, we don’t recurse so this works on deep tries.
        a: _Node[S, V] = self
        b: _Node[S, V] = other
        stack: list[tuple[
            _t.Iterator[tuple[S, _Node[S, V]]],
            _Children[S, V]
        ]] = []
        while True:
            if a.value != b.value or len(a.children) != len(b.children):
                return False
            # len(a.children) == len(b.children) implies they are the same type.

            # Just one child.  Handle without recursion.
            if len(a.children) == 1:
                ac = _t.cast(_OneChild[S, V], a.children)
                bc = _t.cast(_OneChild[S, V], b.children)
                if ac.step != bc.step:
                    return False
                a, b = ac.node, bc.node
                continue

            # Multiple children.  Append to stack.
            if a.children:
                stack.append((iter(a.children.items()),
                              _t.cast(_Children[S, V], b.children)))

            while True:
                try:
                    key, a = next(stack[-1][0])
                    b = stack[-1][1][key]
                    break
                except StopIteration:
                    stack.pop()
                except IndexError:
                    return True
                except KeyError:
                    return False

    def shallow_copy(self, make_copy: _MakeCopy) -> '_Node[S, V]':
        """Returns a copy of the node which shares the children property."""
        cpy: _Node[S, V] = _Node()
        cpy.children = self.children
        cpy.value = make_copy(self.value)
        return cpy

    def copy(self, make_copy: _MakeCopy) -> '_Node[S, V]':
        """Returns a copy of the node structure."""
        cpy = self.shallow_copy(make_copy)
        queue: list[_t.Iterable['_Node[S, V]']] = [(cpy,)]
        while queue:
            for node in queue.pop():
                node.children = node.children.copy(make_copy, queue)
        return cpy

    def __getstate__(self) -> list[int | S | V]:
        """Get state used for pickling.

        The state is encoded as a list of simple commands which consist of an
        integer and some command-dependent number of arguments.  The commands
        modify what the current node is by navigating the trie up and down and
        setting node values.  Possible commands are:

        * [n, step0, step1, ..., stepn-1, value], for n >= 0, specifies step
          needed to reach the next current node as well as its new value.  There
          is no way to create a child node without setting its (or its
          descendant’s) value.

        * [-n], for -n < 0, specifies to go up n steps in the trie.

        When encoded as a state, the commands are flattened into a single list.

        For example::

            [ 0, 'Root',
              2, 'Foo', 'Bar', 'Root/Foo/Bar Node',
             -1,
              1, 'Baz', 'Root/Foo/Baz Node',
             -2,
              1, 'Qux', 'Root/Qux Node' ]

        Creates the following hierarchy::

            -* value: Root
             +-- Foo --* no value
             |         +-- Bar -- * value: Root/Foo/Bar Node
             |         +-- Baz -- * value: Root/Foo/Baz Node
             +-- Qux -- * value: Root/Qux Node

        Returns:
            A pickable state which can be passed to :func:`_Node.__setstate__`
            to reconstruct the node and its full hierarchy.
        """
        # Like iterate, we don’t recurse so pickling works on deep tries.
        state: list[int | S | V] = [] if self.value is _NOVAL else [0]
        last_cmd = 0
        node: _Node[S, V] = self
        stack: list[_t.Iterator[tuple[S, '_Node[S, V]']]] = []
        while True:
            if _is_value(value := node.value):
                last_cmd = 0
                state.append(value)
            stack.append(iter(node.children.items()))

            while True:
                try:
                    step, node = next(stack[-1])
                    break
                except StopIteration:
                    if last_cmd < 0:
                        state[-1] = _t.cast(int, state[-1]) - 1
                    else:
                        last_cmd = -1
                        state.append(-1)
                    stack.pop()
                    if not stack:
                        state.pop()  # Final -n command is not necessary
                        return state

            if last_cmd > 0:
                last_cmd += 1
                state[-last_cmd] = _t.cast(int, state[-last_cmd]) + 1
            else:
                last_cmd = 1
                state.append(1)
            state.append(step)

    def __setstate__(self, state: list[int | S | V]) -> None:
        """Unpickles node.  Cf. :func:`_Node.__getstate__`."""
        self.__init__()  # type: ignore[misc]
        it = iter(state)
        stack: list[_Node[S, V]] = [self]
        for raw_cmd in it:
            cmd = _t.cast(int, raw_cmd)
            if cmd < 0:
                del stack[cmd:]
            else:
                while cmd > 0:
                    parent = stack[-1]
                    step = _t.cast(S, next(it))
                    stack.append(parent.children.add(parent, step))
                    cmd -= 1
                stack[-1].value = _t.cast(V, next(it))


class _NoneStep:
    """Representation of a non-existent step towards a non-existent node.

    The class is private because it should not be constructed by external code.
    Objects of this type are returned by :class:`Trie` methods
    :func:`Trie.shortest_prefix` and :func:`Trie.longest_prefix`.
    """
    __slots__ = ()

    def __bool__(self) -> _t.Literal[False]:
        """Returns whether the object is a valid step, which for ``_NoneStep``
        is always false."""
        return False

    @property
    def key(self) -> None:
        """**Deprecated.** Currently ``None``.  In the future accessing it will
        raise :class:`AttributeError` (cf. :attr:`_Step.key`)."""
        _warnings.warn(
            '_NoneStep.key will soon raise AttributeError; use `bool(step)` to'
            ' check whether step is real or _NoneStep.',
            DeprecationWarning)

    @property
    def value(self) -> None:
        """**Deprecated.** Currently ``None``.  In the future accessing it will
        raise :class:`AttributeError` (cf. :attr:`_Step.value`).

        To safely get value of a step without raising an exception, use
        :func:`_NoneStep.get` method instead.
        """
        _warnings.warn(
            '_NoneStep.value will soon raise AttributeError; use'
            ' `step.get(default)` to get value of a step.',
            DeprecationWarning)

    @property
    def is_set(self) -> _t.Literal[False]:
        """Whether the node has value assigned to it.  Since :class:`_NoneStep`
        represents no node, this is always false."""
        return False

    @property
    def has_subtrie(self) -> _t.Literal[False]:
        """Whether the node has any children.  Since :class:`_NoneStep`
        represents no node, this is always false."""
        return False

    @_t.overload
    def get(self) -> None: ...
    @_t.overload
    def get(self, default: T) -> T: ...
    def get(self, default: T | None=None) -> T | None:
        """Returns node’s value or the default if value is not assigned.
        Since :class:`_NoneStep` represents no node, returns the default."""
        return default

    def __getitem__(self, index: int) -> None:
        """**Deprecated.** Makes object appear like a ``(key, value)`` tuple.

        Prefer ``bool(self)`` to detect whether this is a :class:`_Step` or
        ``_NoneStep``; and :func:`_Step.get` to get value of the node.

        Args:
            index: Element index to return.

        Returns:
            ``None`` if ``index`` is 0 or 1.

        Raises:
            IndexError: If ``index`` is not 0 or 1.
        """
        if index == 0:
            _warnings.warn(
                'Indexed access to _NoneStep is deprecated; use'
                ' `bool(step)` to check whether step is real or _NoneStep.',
                DeprecationWarning)
            return None
        if index == 1:
            _warnings.warn(
                'Indexed access to _NoneStep is deprecated; use'
                ' `step.get(default)` to get value of a step.',
                DeprecationWarning)
            return None
        raise IndexError('index out of range')

    def __repr__(self) -> str:
        return '(None Step)'

    def __setattr__(self, key: str, value: _t.Any) -> None:
        raise AttributeError('_NoneStep is read only')

_NONE_STEP = _NoneStep()


class _Step(_t.Generic[K, V, S]):
    """Representation of a single step on a path towards particular node.

    *Note:* Reading ``value`` property of this class may raise :class:`KeyError`
    if the node at the step does not have a value.  Writing the property always
    succeeds.  :func:`_Step.get` returns value or default and always succeeds.

    The class is private because it should not be constructed by external code.
    Objects of this type are returned by :class:`Trie` methods such as
    :func:`Trie.prefixes` and :func:`Trie.walk_towards`.
    """
    __slots__ = ('_trie', '_path', '_pos', '_node', '__key')

    _trie: 'Trie[K, V, S]'
    _path: _t.Sequence[S]
    _pos: int
    _node: _Node[S, V]
    __key: K

    def __init__(self,
                 trie: 'Trie[K, V, S]',
                 path: _t.Sequence[S],
                 pos: int,
                 node: _Node[S, V]):
        self._trie = trie
        self._path = path
        self._pos = pos
        self._node = node

    def __bool__(self) -> _t.Literal[True]:
        """Returns whether the object is a valid step, which for :class:`_Step`
        is always true."""
        return True

    @property
    def key(self) -> K:
        """Node’s key."""
        if not hasattr(self, '_Step__key'):
            # pylint: disable=protected-access
            self.__key = self._trie._key_from_path(self._path[:self._pos])
        return self.__key

    @property
    def value(self) -> V:
        """Node’s value; on read, raises KeyError if node has no value.

        To safely get value of a step without raising an exception, use
        :func:`_Step.get` method instead.
        """
        if _is_value(value := self._node.value):
            return value
        raise KeyError(self.key)

    @value.setter
    def value(self, value: V) -> None:
        self._node.value = value

    @property
    def is_set(self) -> bool:
        """Whether the node has value assigned to it."""
        return self._node.value is not _NOVAL

    @property
    def has_subtrie(self) -> bool:
        """Whether the node has any children."""
        return bool(self._node.children)

    @_t.overload
    def get(self) -> V | None: ...
    @_t.overload
    def get(self, default: T) -> V | T: ...
    def get(self, default: T | None=None) -> V | T | None:
        """Returns node’s value or the default if value is not assigned."""
        value = self._node.value
        return value if _is_value(value) else default

    def set(self, value: V) -> None:
        """**Deprecated.**  Use ``step.value = value`` instead."""
        _warnings.warn(
            '_Step.set() is deprecated; use `step.value = expr` instead.',
            DeprecationWarning)
        self._node.value = value

    def setdefault(self, value: V) -> V:
        """Assigns value to the node if one is not set then returns it."""
        if _is_value(self._node.value):
            return self._node.value
        self._node.value = value
        return value

    def __str__(self) -> str:
        value = self.get('<no value>')
        return f'({self.key}: {value})'

    def __repr__(self) -> str:
        if _is_value(self._node.value):
            value = repr(self._node.value)
        else:
            value = '<no value>'
        return f'({self.key!r}: {value})'

    @_t.overload
    def __getitem__(self, index: _t.Literal[0]) -> K: ...
    @_t.overload
    def __getitem__(self, index: _t.Literal[1]) -> V: ...
    def __getitem__(self, index: int) -> K | V:
        """Makes object appear like a ``(key, value)`` tuple.

        Cf. :attr:`_Step.key`, :attr:`_Step.value` and :func:`_Step.get`.

        Args:
            index: Element index to return.

        Returns:
            ``self.key`` if ``index`` is 0, ``self.value`` if ``index`` is 1.

        Raises:
            IndexError: If ``index`` is not 0 or 1.
            KeyError: If ``index`` is 1 and the node has no value.
        """
        if index == 0:
            return self.key
        if index == 1:
            return self.value
        raise IndexError('index out of range')


_Trace = list[tuple[S, _Node[S, V]]]


class Trie(_t.Generic[K, V, S], _abc.MutableMapping[K, V]):
    """A trie implementation with dict interface plus some extensions.

    **Typing:** The class has three generic arguments: ``K``, ``V`` and
    ``S``. ``K`` and ``V`` are the types of keys and values respectively.  Keys
    must be iterables of hashable objects, called `steps`.  In other words, for
    a given key, ``dict.fromkeys(key)`` must be valid expression.  ``S`` is type
    of those steps, i.e. an item returned when iterated over the key.  For
    example, if ``K`` is ``tuple[int, ...]`` then ``S`` needs to be ``int``.

    Due to limited expressiveness of Python’s type system, the ``S`` type has to
    be specified explicitly and it’s possible to declare incompatible generic
    argument combinations.  For example, ``Trie[tuple[int, ...], V, str]`` makes
    no sense.

    Furthermore, methods which return keys (e.g. :func:`keys` or
    :func:`popitem`) always return them as ``tuple[S, ...]`` (regardless of
    ``K``).  As a result, some combination of generic arguments may lead to
    unsound type inference.  For example::

        >>> import typing
        >>> import pygtrie

        >>> trie: pygtrie.Trie[str, bool, str] = pygtrie.Trie(foo=True)
        >>> key: str = trie.keys()[0]
        >>> # ‘keys` method’s declare return type is `list[K]` hence why type of
        >>> # `trie.keys()[0]` expression is `K` which is `str` in the example.
        >>> # However, in reality
        >>> isinstance(key, str)
        False
        >>> key
        ('f', 'o', 'o')

    The problem can be solved in a few ways.  First, use ``tuple[S, ...]``,
    ``typing.Sequence[S]`` or ``typing.Iterable[S]`` as the `K` generic
    argument.  This is robust because all type inference remains sound.  For
    example::

        >>> t: pygtrie.Trie[typing.Sequence[str], bool, str] = pygtrie.Trie()
        >>> t['foo'] = True
        >>> t[('b', 'a', 'r')] = True
        >>> keys: list[typing.Sequence[str]] = t.keys()
        >>> print([type(key).__name__ for key in keys])
        ['tuple', 'tuple']
        >>> print(keys)
        [('f', 'o', 'o'), ('b', 'a', 'r')]

    Second, use an existing subclass such as :class:`CharTrie` and
    :class:`StringTrie`.  This is best if the key is a string as the classes
    properly convert a sequence of string steps into a single string:

        >>> t: pygtrie.CharTrie[bool] = pygtrie.CharTrie(foo=True)
        >>> key: str = t.keys()[0]
        >>> isinstance(key, str)
        True
        >>> key
        'foo'

    Third, define your own subclass which does desired conversion key-steps
    conversion.  See below for an example.

    Fourth, upon getting a key from the trie, convert it to desired type.  This
    is fragile as type inference cannot detect issues and it’s easy to forget
    about the conversion.

    **Subclassing:** Subclasses can modify the way keys are iterated over by
    overriding :func:`_path_from_key` and :func:`_key_from_path`.  For example,
    consider a trie whose keys are paths::

        class PathTrie(pygtrie.Trie[pathlib.Path, V, str]):
            def _path_from_key(self, key: pathlib.Path) -> tuple[str, ...]:
                return key.parts()

            def _key_from_path(self,
                               path: typing.Iterable[str]) -> pathlib.Path:
                return pathlib.Path(*path)

    Note on terminology: keys are converted into `paths` which are sequences of
    `steps`.  Steps correspond to labels on the trie vertices and path defines
    how to get from root node to a particular node.
    """

    _root: _Node[S, V]
    _items_callback: _t.Callable[[_AnyChildren[S, V]],
                                 _t.Iterable[tuple[S, _Node[S, V]]]]

    @_t.overload
    def __init__(self, other: _SupportsKeysAndGetItem[K, V], /) -> None: ...
    @_t.overload
    def __init__(self, other: _t.Iterable[tuple[K, V]]=(), /) -> None: ...
    @_t.overload
    def __init__(self: 'Trie[K | str, V, S]',
                 other: _SupportsKeysAndGetItem[K, V], /,
                 **kwargs: V) -> None: ...
    @_t.overload
    def __init__(self: 'Trie[K | str, V, S]',
                 other: _t.Iterable[tuple[K, V]]=(), /,
                 **kwargs: V) -> None: ...

    def __init__(self, other: _Other[K, V]=(), /, **kwargs: V) -> None:
        """Initialises the trie.

        Arguments are interpreted the same way :func:`update` interprets them.
        """
        self._root = _Node()
        self._items_callback = self._ITEMS_CALLBACKS[0]
        self.update(other, **kwargs)

    _ITEMS_CALLBACKS = (lambda x: x.items(), lambda x: x.sorted_items())

    @_t.overload
    def update(self, other: _SupportsKeysAndGetItem[K, V], /) -> None: ...
    @_t.overload
    def update(self, other: _t.Iterable[tuple[K, V]]=(), /) -> None: ...
    @_t.overload
    def update(self: 'Trie[K | str, V, S]',
               other: _SupportsKeysAndGetItem[K, V],
               /, **kwargs: V) -> None: ...
    @_t.overload
    def update(self: 'Trie[K | str, V, S]',
               other: _t.Iterable[tuple[K, V]]=(),
               /, **kwargs: V) -> None: ...
    def update(self, other: _Other[K, V]=(), /, **kwargs: V) -> None:
        """Updates stored values.  Works like :meth:`dict.update`.

        Args:
            other: Mapping or iterable of ``(key, value)`` pairs to update the
                trie with.
            **kwargs: Mapping from strings (names of keyword arguments) to
                values.  May be specified if the trie’s keys accept strings
                only.
        """
        if isinstance(other, Trie):
            # MutableMapping.update() does `for key in other: self[key] =
            # other[key]` which performs key lookup twice.  If we’re dealing
            # with a Trie, that’s quite expensive so convert `other` to an
            # iterator over items of the trie.
            other = other.items()
        super().update(other, **kwargs)

    def enable_sorting(self, enable: bool=True) -> None:
        """Enables sorting the child nodes when iterating and traversing.

        Normally, child nodes are not sorted when iterating or traversing over
        the trie (just like :class:`dict` elements are not sorted).  This method
        allows sorting to be enabled (which was the behaviour prior to pygtrie
        2.0 release).

        For Trie class, enabling sorting the child nodes is identical to sorting
        the list of items since Trie returns keys as tuples.  However, in
        subclasses such as StringTrie, the two may behave differently.  For
        example, sorting items might produce::

            root/foo-bar
            root/foo/baz

        even though foo comes before foo-bar.

        Args:
            enable: Whether to enable sorting the child nodes.
        """
        self._items_callback = self._ITEMS_CALLBACKS[bool(enable)]

    def __getstate__(self) -> dict[str, _t.Any]:
        # encode self._items_callback as self._sorted when pickling
        state = self.__dict__.copy()
        callback = state.pop('_items_callback', None)
        state['_sorted'] = callback is self._ITEMS_CALLBACKS[1]
        return state

    def __setstate__(self, state: dict[str, _t.Any]) -> None:
        # translate self._sorted back to _items_callback when unpickling
        self.__dict__ = state
        self.enable_sorting(state.pop('_sorted'))

    def clear(self) -> None:
        """Removes all the values from the trie."""
        self._root = _Node()

    def merge(self, other: 'Trie[K, V, S]', overwrite: bool=False) -> None:
        """Moves nodes from other trie into this one.

        The merging happens at trie structure level and as such is different
        than iterating over items of one trie and setting them in the other
        trie.

        The merging may happen between different types of tries resulting in
        different (key, value) pairs in the destination trie compared to the
        source.  For example, merging two :class:`StringTrie` objects each using
        different separators will work as if the other trie had separator of
        this trie.  Similarly, a :class:`CharTrie` may be merged into
        a :class:`StringTrie` but when keys are read those will be joined by the
        separator.  For example:

            >>> import pygtrie
            >>> st = pygtrie.StringTrie(separator='.')
            >>> st.merge(pygtrie.StringTrie({'foo/bar': 42}))
            >>> list(st.items())
            [('foo.bar', 42)]
            >>> st.merge(pygtrie.CharTrie({'baz': 24}))
            >>> sorted(st.items())
            [('b.a.z', 24), ('foo.bar', 42)]

        Not all tries can be merged into other tries.  For example,
        a :class:`StringTrie` may not be merged into a :class:`CharTrie` because
        the latter imposes a requirement for each component in the key to be
        exactly one character while in the former components may be of arbitrary
        length.

        Note that the other trie is cleared and any references or iterators over
        it are invalidated.  To preserve other’s value it needs to be copied
        first.

        Args:
            other: Other trie to move nodes from.
            overwrite: Whether to overwrite existing values in this trie.
        """
        if isinstance(self, type(other)):
            self._merge_impl(self, other, overwrite=overwrite)
        else:
            other._merge_impl(self, other, overwrite=overwrite)  # pylint: disable=protected-access
        other.clear()

    @classmethod
    def _merge_impl(cls, dst: _t.Self, src: _t.Self, overwrite: bool) -> None:
        # pylint: disable=protected-access
        dst._root.merge(src._root, overwrite=overwrite)

    def __copy(self, make_copy: _MakeCopy=lambda x: x) -> _t.Self:
        """Returns a shallow copy of the object.

        Args:
            make_copy: Function copying values.  If not given, values won’t be
                copied.
        """
        cpy: _t.Self = self.__class__()
        cpy.__dict__ = dict(self.__dict__, _root=self._root.copy(make_copy))
        return cpy

    def copy(self) -> _t.Self:
        """Returns a shallow copy of the object."""
        return self.__copy()

    def __copy__(self) -> _t.Self:
        return self.__copy()

    def __deepcopy__(self, memo: _t.Any) -> _t.Self:
        def _deep_copy(value: T) -> T:
            return _copy.deepcopy(value, memo)
        return self.__copy(_deep_copy)

    @_t.overload
    @classmethod
    def fromkeys(cls, keys: _t.Iterable[K]) -> 'Trie[K, V | None, S]': ...
    @_t.overload
    @classmethod
    def fromkeys(cls, keys: _t.Iterable[K], value: V) -> 'Trie[K, V, S]': ...
    @classmethod
    def fromkeys(
            cls, keys: _t.Iterable[K], value: V | None=None
    ) -> _t.Union['Trie[K, V, S]', 'Trie[K, V | None, S]']:
        """Returns a new trie with given ``keys`` set to provided ``value``.

        This is equivalent to calling the constructor with a ``(key, value) for
        key in keys`` generator.

        **Typing:** Calling the method without ``value`` argument specified is
        valid only if the trie can store ``None`` values (i.e. when the trie’s
        ``V`` generic argument accepts ``None``).

        Args:
            keys: An iterable of keys that should be set in the new trie.
            value: Value to associate with given keys.  The value is not copied;
                all keys reference the same object.
        """
        v = _t.cast(V, value)
        return cls((key, v) for key in keys)

    def _get_node(self, key: K | _Sentinel) -> tuple[_Node[S, V], _Trace[S, V]]:
        """Returns node for given key.  Creates it if requested.

        Args:
            key: A key to look for.

        Returns:
            ``(node, trace)`` tuple where ``node`` is the node for given key and
            ``trace`` is a list specifying path to reach the node including all
            the encountered nodes.  Each element of trace is a ``(step, node)``
            tuple where ``step`` is a step from parent node to given node and
            ``node`` is node on the path.  The first element of the path is
            always ``(None, self._root)``.

        Raises:
            KeyError: If there is no node for the key.
        """
        node = self._root
        trace: list[tuple[S | None, _Node[S, V]]] = [(None, node)]
        for step in self.__path_from_key(key):
            n = node.children.get(step)
            if n is None:
                raise KeyError(key)
            node = n
            trace.append((step, node))
        # The first element of trace has a `None` step, but we’re lying about
        # the type to make the rest of the code less noisy.  In practice, the
        # first step is never accessed and the first element is only used to
        # keep the root node.
        return node, _t.cast(_Trace[S, V], trace)

    def _set_node(self,
                  key: K,
                  value: V,
                  only_if_missing: bool=False) -> _Node[S, V]:
        """Sets value for a given key.

        Args:
            key: Key to set value of.
            value: Value to set to.
            only_if_missing: If true, value won’t be changed if the key is
                already associated with a value.

        Returns:
            The node.
        """
        node = self._root
        for step in self.__path_from_key(key):
            node = node.children.require(node, step)
        if node.value is _NOVAL or not only_if_missing:
            node.value = value
        return node

    def _set_node_if_no_prefix(self, key: K) -> None:
        """Sets given key to True but only if none of its prefixes are present.

        If value is set, removes all descendants of the node.

        This is a method for exclusive use by :class:`PrefixSet`.

        Args:
            key: Key to set value of.
        """
        steps = iter(self.__path_from_key(key))
        node = self._root
        try:
            while node.value is _NOVAL:
                node = node.children.require(node, next(steps))
        except StopIteration:
            # This method is only used when V is bool.
            node.value = _t.cast(V, True)
            node.children = _NoChildren()

    def __iter__(self) -> _t.Iterator[K]:
        return self.iterkeys()

    def iteritems(self,
                  prefix: K | _Sentinel=_SENTINEL,
                  shallow: bool=False) -> _t.Iterator[tuple[K, V]]:
        """Yields all nodes with associated values with given prefix.

        Only nodes with values are output.  For example::

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo'] = 'Foo'
            >>> t['foo/bar/baz'] = 'Baz'
            >>> t['qux'] = 'Qux'
            >>> sorted(t.items())
            [('foo', 'Foo'), ('foo/bar/baz', 'Baz'), ('qux', 'Qux')]

        Items are output in topological order (i.e. parents before children) but
        the order of siblings is unspecified.  At the expense of efficiency,
        :func:`enable_sorting` can make ordering of siblings deterministic.

        With ``prefix`` argument, only items with specified prefix are generated
        (i.e. only given subtrie is traversed) as demonstrated by::

            >>> t.items(prefix='foo')
            [('foo', 'Foo'), ('foo/bar/baz', 'Baz')]

        With ``shallow`` argument, if a node has a value associated with it, its
        children are not traversed even if they exist which can be seen in::

            >>> sorted(t.items(shallow=True))
            [('foo', 'Foo'), ('qux', 'Qux')]

        Args:
            prefix: If given, prefix to limit iteration to.
            shallow: Perform a shallow traversal, i.e. do not yield items if
                their prefix has been yielded.

        Yields:
            ``(key, value)`` tuples.

        Raises:
            KeyError: If ``prefix`` does not match any node.
        """
        node, _ = self._get_node(prefix)
        for path, value in node.iterate(list(self.__path_from_key(prefix)),
                                        shallow, self._items_callback):
            yield (self._key_from_path(path), value)

    def iterkeys(self,
                 prefix: K | _Sentinel=_SENTINEL,
                 shallow: bool=False) -> _t.Iterator[K]:
        """Yields all keys having associated values with given prefix.

        This is equivalent to taking first element of tuples generated by
        :func:`iteritems`.

        Args:
            prefix: If given, prefix to limit iteration to.
            shallow: Perform a shallow traversal, i.e. do not yield keys if
                their prefix has been yielded.

        Yields:
            All the keys (with given prefix) with associated values in the trie.

        Raises:
            KeyError: If ``prefix`` does not match any node.
        """
        for key, _ in self.iteritems(prefix=prefix, shallow=shallow):
            yield key

    def itervalues(self,
                   prefix: K | _Sentinel=_SENTINEL,
                   shallow: bool=False) -> _t.Iterator[V]:
        """Yields all values associated with keys with given prefix.

        This is equivalent to taking second element of tuples generated by
        :func:`iteritems`.

        Args:
            prefix: If given, prefix to limit iteration to.
            shallow: Perform a shallow traversal, i.e. do not yield values if
                their prefix has been yielded.

        Yields:
            All the values associated with keys (with given prefix) in the trie.

        Raises:
            KeyError: If ``prefix`` does not match any node.
        """
        node, _ = self._get_node(prefix)
        for _, value in node.iterate(list(self.__path_from_key(prefix)),
                                     shallow, self._items_callback):
            yield value

    # collections.abc.MutableMapping uses ItemsView, KeysView and ValuesView as
    # return types of items, keys and values methods.  However, we don’t want to
    # use those types and mypy doesn’t recognise that a list is compatible type.
    # For now, ignore the type error.

    def items(self,  # type: ignore[override]
              prefix: K | _Sentinel=_SENTINEL,
              shallow: bool=False) -> list[tuple[K, V]]:
        """Returns a list of ``(key, value)`` pairs in given subtrie.

        This is equivalent to constructing a list from generator returned by
        :func:`iteritems`.
        """
        return list(self.iteritems(prefix=prefix, shallow=shallow))

    def keys(self,  # type: ignore[override]
             prefix: K | _Sentinel=_SENTINEL,
             shallow: bool=False) -> list[K]:
        """Returns a list of all the keys, with given prefix, in the trie.

        This is equivalent to constructing a list from generator returned by
        :func:`iterkeys`.
        """
        return list(self.iterkeys(prefix=prefix, shallow=shallow))

    def values(self,  # type: ignore[override]
               prefix: K | _Sentinel=_SENTINEL,
               shallow: bool=False) -> list[V]:
        """Returns a list of values in given subtrie.

        This is equivalent to constructing a list from generator returned by
        :func:`itervalues`.
        """
        return list(self.itervalues(prefix=prefix, shallow=shallow))

    def __len__(self) -> int:
        """Returns the number of values in the trie.

        This method is expensive to run as it iterates over the whole trie.
        """
        return sum(1 for _ in self.itervalues())

    def __bool__(self) -> bool:
        return self._root.value is not _NOVAL or bool(self._root.children)

    __hash__ = None  # type: ignore[assignment]

    #: Bit returned by :func:`has_node` to indicate that the node has value
    #: assigned to it.
    HAS_VALUE = 1
    #: Bit returned by :func:`has_node` to indicate that the node has child
    #: nodes.
    HAS_SUBTRIE = 2

    def has_node(self, key: K) -> int:
        """Returns whether given node is in the trie.

        Return value is a bitwise OR of :attr:`HAS_VALUE` and
        :attr:`HAS_SUBTRIE` constants indicating node has a value associated
        with it and that it is a prefix of another existing key respectively.
        Both of those are independent of each other and all of the four
        combinations are possible.  For example::

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo/bar'] = 'Bar'
            >>> t['foo/bar/baz'] = 'Baz'
            >>> t.has_node('qux') == 0
            True
            >>> t.has_node('foo/bar/baz') == pygtrie.Trie.HAS_VALUE
            True
            >>> t.has_node('foo') == pygtrie.Trie.HAS_SUBTRIE
            True
            >>> t.has_node('foo/bar') == (pygtrie.Trie.HAS_VALUE |
            ...                           pygtrie.Trie.HAS_SUBTRIE)
            True

        There are two higher level methods built on top of this one which give
        easier interface for the information. :func:`has_key` returns whether
        node has a value associated with it and :func:`has_subtrie` checks
        whether node is a prefix.  Continuing previous example::

            >>> t.has_key('qux'), t.has_subtrie('qux')
            (False, False)
            >>> t.has_key('foo/bar/baz'), t.has_subtrie('foo/bar/baz')
            (True, False)
            >>> t.has_key('foo'), t.has_subtrie('foo')
            (False, True)
            >>> t.has_key('foo/bar'), t.has_subtrie('foo/bar')
            (True, True)

        Args:
            key: A key to look for.

        Returns:
            Non-zero if node exists and if it does a bit-field denoting whether
            it has a value associated with it and whether it has a subtrie.
        """
        try:
            node, _ = self._get_node(key)
        except KeyError:
            return 0
        return ((self.HAS_VALUE * (node.value is not _NOVAL)) |
                (self.HAS_SUBTRIE * bool(node.children)))

    def has_key(self, key: K) -> bool:
        """Indicates whether given key has value associated with it.
        Cf. :func:`has_node`."""
        return bool(self.has_node(key) & self.HAS_VALUE)

    def has_subtrie(self, key: K) -> bool:
        """Returns whether given key is a prefix of another key in the trie.
        Cf. :func:`has_node`."""
        return bool(self.has_node(key) & self.HAS_SUBTRIE)

    @staticmethod
    def _slice_maybe(key_or_slice: K | slice) -> tuple[K, bool]:
        """Checks whether argument is a slice or a plain key.

        Args:
            key_or_slice: A key or a slice to test.

        Returns:
            ``(key, is_slice)`` tuple.  ``is_slice`` indicates whether
            ``key_or_slice`` is a slice and ``key`` is either ``key_or_slice``
            itself (if it’s not a slice) or slice’s start position.

        Raises:
            TypeError: If ``key_or_slice`` is a slice whose stop or step are not
                ``None`` In other words, only ``[key:]`` slices are valid.
        """
        if not isinstance(key_or_slice, slice):
            return key_or_slice, False
        elif key_or_slice.stop is None and key_or_slice.step is None:
            return _t.cast(K, key_or_slice.start), True
        else:
            raise TypeError(key_or_slice)

    @_t.overload
    def __getitem__(self, key_or_slice: K) -> V: ...
    @_t.overload
    def __getitem__(self, key_or_slice: slice) -> _t.Iterator[V]: ...
    def __getitem__(self, key_or_slice: K | slice) -> V | _t.Iterator[V]:
        """Returns value associated with given key or raises :class:`KeyError`.

        When argument is a single key, value for that key is returned (or
        :class:`KeyError` exception is thrown if the node does not exist or has
        no value associated with it).

        When argument is a slice, it must be one with only ``start`` set in
        which case the access is identical to :func:`itervalues` invocation with
        prefix argument.

        Example:

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo/bar'] = 'Bar'
            >>> t['foo/baz'] = 'Baz'
            >>> t['qux'] = 'Qux'
            >>> t['foo/bar']
            'Bar'
            >>> sorted(t['foo':])
            ['Bar', 'Baz']
            >>> t['foo']  # doctest: +IGNORE_EXCEPTION_DETAIL
            Traceback (most recent call last):
                ...
            ShortKeyError: 'foo'

        Args:
            key_or_slice: A key or a slice to look for.

        Returns:
            If a single key is passed, a value associated with given key.  If
            a slice is passed, a generator of values in specified subtrie.

        Raises:
            ShortKeyError: If the key has no value associated with it but is
                a prefix of some key with a value.  Note :class:`ShortKeyError`
                is a subclass of :class:`KeyError`.
            KeyError: If key has no value associated with it nor is a prefix of
                an existing key.
            TypeError: If ``key_or_slice`` is a slice but its stop or step are
                not ``None``.
        """
        key, is_slice = self._slice_maybe(key_or_slice)
        if is_slice:
            return self.itervalues(key)
        node, _ = self._get_node(key)
        if _is_value(value := node.value):
            return value
        raise ShortKeyError(key)

    def __setitem__(self, key_or_slice: K | slice, value: V) -> None:
        """Sets value associated with given key.

        If ``key_or_slice`` is a key, associates it with given value.  If it is
        a slice (which must have ``start`` set only), in addition clears any
        subtrie that might have been attached to particular key.  For example::

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo/bar'] = 'Bar'
            >>> t['foo/baz'] = 'Baz'
            >>> sorted(t.keys())
            ['foo/bar', 'foo/baz']
            >>> t['foo':] = 'Foo'
            >>> t.keys()
            ['foo']

        Args:
            key_or_slice: A key to look for or a slice.  If it is a slice, the
                whole subtrie (if present) will be replaced by a single node
                with given value set.
            value: Value to set.

        Raises:
            TypeError: If key is a slice whose stop or step are not None.
        """
        key, is_slice = self._slice_maybe(key_or_slice)
        node = self._set_node(key, value)
        if is_slice:
            node.children = _NoChildren()

    @_t.overload
    def setdefault(self: 'Trie[K, V | None, S]', key: K) -> V | None: ...
    @_t.overload
    def setdefault(self, key: K, default: V) -> V: ...
    def setdefault(self, key: K, default: V | None=None) -> V | None:
        """Sets value of a given node if not set already.  Also returns it.

        In contrast to :func:`__setitem__`, this method does not accept slice as
        a key.

        **Typing:** Calling the method without ``default`` argument specified is
        valid only if the trie can store ``None`` values (i.e. when the trie’s
        ``V`` generic argument accepts ``None``).
        """
        node = self._set_node(key, _t.cast(V, default), only_if_missing=True)
        return _t.cast(V, node.value)

    @staticmethod
    def _pop_value(trace: _Trace[S, V]) -> V | _NoValue:
        """Removes value from given node and removes any empty nodes.

        Args:
            trace: Trace to the node to cleanup as returned by
                :func:`_get_node`.  The last element of the trace denotes the
                node to get value of.

        Returns:
            Value which was held in the node at the end of specified trace.
            This may be ``_NOVAL`` if the node didn’t have a value in the first
            place.
        """
        i = len(trace) - 1  # len(path) >= 1 since root is always there
        step, node = trace[i]
        value, node.value = node.value, _NOVAL
        while i and node.value is _NOVAL and not node.children:
            i -= 1
            parent_step, parent = trace[i]
            parent.children.delete(parent, step)
            step, node = parent_step, parent
        return value

    def pop(self, key: K, default: T | _Sentinel=_SENTINEL) -> V | T:
        """Deletes value associated with given key and returns it.

        Args:
            key: A key to look for.
            default: If specified, value that will be returned if given key has
                no value associated with it.  If not specified, method will
                throw :class:`KeyError` in such cases.

        Returns:
            Removed value, if key had value associated with it, or ``default``
            (if given).

        Raises:
            ShortKeyError: If ``default`` has not been specified and the key has
                no value associated with it but is a prefix of some key with
                a value.  Note :class:`ShortKeyError` is a subclass of
                :class:`KeyError`.
            KeyError: If default has not been specified and key has no value
                associated with it nor is a prefix of an existing key.
        """
        try:
            _, trace = self._get_node(key)
            value = self._pop_value(trace)
            if _is_value(value):
                return value
            raise ShortKeyError(key)
        except KeyError:
            if _is_not_sentinel(default):
                return default
            raise

    def popitem(self) -> tuple[K, V]:
        """Deletes an arbitrary value from the trie and returns it.

        There is no guarantee as to which item is deleted and returned, whether
        in terms of lexicographical or topological order.

        Returns:
            ``(key, value)`` tuple indicating deleted key.

        Raises:
            KeyError: If the trie is empty.
        """
        if not self:
            raise KeyError()
        node = self._root
        # The first element of the trace is never accessed.  We lie about its
        # type to simplify the code and avoid redundant casts and checks.
        trace: _Trace[S, V] = [(_t.cast(S, None), node)]
        while not _is_value(value := node.value):
            # If node has no value, it must have children.
            step, node = node.children.pick()
            trace.append((step, node))
        key = self._key_from_path((step for step, _ in trace[1:]))
        self._pop_value(trace)
        return key, value

    def __delitem__(self, key_or_slice: K | slice) -> None:
        """Deletes value associated with given key or raises KeyError.

        If argument is a key, value associated with it is deleted.  If the key
        is also a prefix, its descendants are not affected.  On the other hand,
        if the argument is a slice (in which case it must have only start set),
        the whole subtrie is removed.  For example::

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo'] = 'Foo'
            >>> t['foo/bar'] = 'Bar'
            >>> t['foo/bar/baz'] = 'Baz'
            >>> del t['foo/bar']
            >>> t.keys()
            ['foo', 'foo/bar/baz']
            >>> del t['foo':]
            >>> t.keys()
            []

        Args:
            key_or_slice: A key to look for or a slice.  If key is a slice, the
                    whole subtrie will be removed.

        Raises:
            ShortKeyError: If the key has no value associated with it but is
                a prefix of some key with a value.  This is not thrown if
                key_or_slice is a slice -- in such cases, the whole subtrie is
                removed.  Note :class:`ShortKeyError` is a subclass of
                :class:`KeyError`.
            KeyError: If key has no value associated with it nor is a prefix of
                an existing key.
            TypeError: If key is a slice whose stop or step are not ``None``.
        """
        key, is_slice = self._slice_maybe(key_or_slice)
        node, trace = self._get_node(key)
        if is_slice:
            node.children = _NoChildren()
        elif node.value is _NOVAL:
            raise ShortKeyError(key)
        self._pop_value(trace)

    # Re-exported here mostly for backwards compatibility.
    _Step: _t.TypeAlias = _Step
    _NoneStep: _t.TypeAlias = _NoneStep

    def walk_towards(self, key: K) -> _t.Iterator[_Step[K, V, S]]:
        """Yields nodes on the path to given node.

        Args:
            key: Key of the node to look for.

        Yields:
            :class:`_Step` objects which can be used to extract or set node’s
            value as well as get node’s key.

            When representing nodes with assigned values, the objects can be
            treated as ``(k, value)`` pairs denoting keys with associated values
            encountered on the way towards the specified key.  This is
            deprecated, prefer using ``key`` and ``value`` properties or ``get``
            method of the object.

        Raises:
            KeyError: If node with given key does not exist.  it’s all right if
                the value is not assigned to the node provided it has a child
                node.  Because the method is a generator, the exception is
                raised only once a missing node is encountered.
        """
        node = self._root
        path = self.__path_from_key(key)
        pos = 0
        while True:
            yield _Step(self, path, pos, node)
            if pos == len(path):
                break
            n = node.children.get(path[pos])
            if n is None:
                raise KeyError(key)
            node = n
            pos += 1

    def prefixes(self, key: K) -> _t.Iterator[_Step[K, V, S]]:
        """Walks towards the node specified by key and yields all found items.

        Example:

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo'] = 'Foo'
            >>> t['foo/bar/baz'] = 'Baz'
            >>> list(t.prefixes('foo/bar/baz/qux'))
            [('foo': 'Foo'), ('foo/bar/baz': 'Baz')]
            >>> list(t.prefixes('does/not/exist'))
            []

        Args:
            key: Key to look for.

        Yields:
            :class:`_Step` objects which can be used to extract or set node’s
            value and get its key.

            The objects can be treated as ``(k, value)`` pairs denoting keys
            with associated values encountered on the way towards the specified
            key.  This is deprecated, prefer using ``key`` and ``value``
            properties of the object.
        """
        try:
            for step in self.walk_towards(key):
                if step.is_set:
                    yield step
        except KeyError:
            pass

    def shortest_prefix(self, key: K) -> _NoneStep | _Step[K, V, S]:
        """Finds the shortest prefix of a key with a value.

        This is equivalent to taking the first item yielded by the
        :func:`prefixes` method with additional handling of situations when no
        prefixes are found.

        Example:

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo'] = 'Foo'
            >>> t['foo/bar/baz'] = 'Baz'
            >>> t.shortest_prefix('foo/bar/baz/qux')
            ('foo': 'Foo')
            >>> t.shortest_prefix('foo/bar/baz/qux').key
            'foo'
            >>> t.shortest_prefix('foo/bar/baz/qux').value
            'Foo'
            >>> t.shortest_prefix('does/not/exist')
            (None Step)
            >>> bool(t.shortest_prefix('does/not/exist'))
            False

        Args:
            key: Key to look for.

        Returns:
            :class:`_Step` object (which can be used to extract or set node’s
            value and get its key), or a :class:`_NoneStep` object (which is
            falsy value) if no prefix is found.
        """
        return next(self.prefixes(key), _NONE_STEP)

    def longest_prefix(self, key: K) -> _NoneStep | _Step[K, V, S]:
        """Finds the longest prefix of a key with a value.

        This is equivalent to taking the last item yielded by the
        :func:`prefixes` method with additional handling of situations when no
        prefixes are found.

        Example:

            >>> import pygtrie
            >>> t = pygtrie.StringTrie()
            >>> t['foo'] = 'Foo'
            >>> t['foo/bar/baz'] = 'Baz'
            >>> t.longest_prefix('foo/bar/baz/qux')
            ('foo/bar/baz': 'Baz')
            >>> t.longest_prefix('foo/bar/baz/qux').key
            'foo/bar/baz'
            >>> t.longest_prefix('foo/bar/baz/qux').value
            'Baz'
            >>> t.longest_prefix('does/not/exist')
            (None Step)
            >>> bool(t.longest_prefix('does/not/exist'))
            False

        Args:
            key: Key to look for.

        Returns:
            :class:`_Step` object (which can be used to extract or set node’s
            value as well as get node’s key), or a :class:`_NoneStep` object
            (which is a falsy value).
        """
        ret: _NoneStep | _Step[K, V, S] = _NONE_STEP
        for ret in self.prefixes(key):
            pass
        return ret

    def strictly_equals(self, other: 'Trie[K, V, S]') -> bool:
        """Returns whether tries are equal with the same structure.

        This is stricter comparison than the one performed by equality operator.
        It not only requires keys and values to be equal but also the two tries
        to be of the same type and have the same structure.

        For example, two :class:`StringTrie` objects to compare strictly equal
        if they have the same structure as well as the same separator.

        Example:

            >>> import pygtrie
            >>> t0 = StringTrie({'foo/bar.baz': 42}, separator='/')
            >>> t1 = StringTrie({'foo/bar.baz': 42}, separator='.')
            >>> t0 == t1
            True
            >>> t0.strictly_equals(t1)
            False

        Args:
            other: Other trie to compare to.
        """
        if self is other:
            return True
        if type(self) is not type(other):
            return False
        result = self._eq_impl(other)
        if result is NotImplemented:
            return False
        else:
            return result

    def __eq__(self, other: object) -> bool:
        """Compares this trie’s mapping with another mapping.

        Note that this method doesn’t take trie’s structure into consideration.
        What matters is whether keys and values in both mappings are the same.
        This may lead to unexpected results, for example:

            >>> import pygtrie
            >>> t0 = StringTrie({'foo/bar': 42}, separator='/')
            >>> t1 = StringTrie({'foo.bar': 42}, separator='.')
            >>> t0 == t1
            False

            >>> t0 = StringTrie({'foo/bar.baz': 42}, separator='/')
            >>> t1 = StringTrie({'foo/bar.baz': 42}, separator='.')
            >>> t0 == t1
            True

            >>> t0 = Trie({'foo': 42})
            >>> t1 = CharTrie({'foo': 42})
            >>> t0 == t1
            False

        This behaviour is required to maintain consistency with Mapping
        interface and its __eq__ method.  For example, this implementation
        maintains transitivity of the comparison:

            >>> t0 = StringTrie({'foo/bar.baz': 42}, separator='/')
            >>> d = {'foo/bar.baz': 42}
            >>> t1 = StringTrie({'foo/bar.baz': 42}, separator='.')
            >>> t0 == d
            True
            >>> d == t1
            True
            >>> t0 == t1
            True

            >>> t0 = Trie({'foo': 42})
            >>> d = {'foo': 42}
            >>> t1 = CharTrie({'foo': 42})
            >>> t0 == d
            False
            >>> d == t1
            True
            >>> t0 == t1
            False

        Args:
            other: Other object to compare to.

        Returns:
            ``NotImplemented`` if this method does not know how to perform the
            comparison or a ``bool`` denoting whether the two objects are equal
            or not.
        """
        if self is other:
            return True
        if type(other) is type(self):
            result = self._eq_impl(other)
            if result is not NotImplemented:
                return result
        return super().__eq__(other)

    def _eq_impl(self, other: _t.Self) -> bool | _types.NotImplementedType:
        return self._root.equals(other._root)  # pylint: disable=protected-access

    def __ne__(self, other: object) -> bool:
        return not self == other

    def _str_items(self, fmt: str='%s: %s') -> str:
        return ', '.join(fmt % item for item in self.iteritems())

    def __str__(self) -> str:
        return '%s(%s)' % (type(self).__name__, self._str_items())

    def __repr__(self) -> str:
        return '%s([%s])' % (type(self).__name__, self._str_items('(%r, %r)'))

    def __path_from_key(self, key: K | _Sentinel) -> _t.Sequence[S]:
        """Converts a user visible key object to internal path representation.

        Args:
            key: User supplied key or ``_SENTINEL``.

        Returns:
            An empty tuple if ``key`` is ``_SENTINEL``, otherwise whatever
            :func:`_path_from_key` returns.

        Raises:
            TypeError: If ``key`` is of invalid type.
        """
        return self._path_from_key(key) if _is_not_sentinel(key) else ()

    def _path_from_key(self, key: K) -> _t.Sequence[S]:
        """Converts a user visible key object to internal path representation.

        The default implementation returns the key.  Subclasses may override
        this method (together with :func:`_key_from_path`) to support keys of
        other types, e.g. splitting a string key into path components.

        Args:
            key: User supplied key.

        Returns:
            A path, which is an iterable of steps.  Each step must be hashable.

        Raises:
            TypeError: If ``key`` is of invalid type.
        """
        return _t.cast(_t.Sequence[S], key)

    def _key_from_path(self, path: _t.Iterable[S]) -> K:
        """Converts an internal path into a user visible key object.

        The default implementation creates a tuple from the path.  Subclasses
        may override this method (together with :func:`_path_from_key`) to
        support keys of other types, e.g. splitting a string key into path
        components.

        Args:
            path: Internal path representation.
        Returns:
            A user visible key object.
        """
        return _t.cast(K, tuple(path))

    def traverse(
            self,
            node_factory: NodeFactory[K, V, S, T],
            prefix: K | _Sentinel=_SENTINEL) -> T:
        """Traverses the tree using node_factory object.

        ``node_factory`` is a callable which accepts ``(key_from_path, path,
        children, value=...)`` arguments, where ``key_from_path`` converts paths
        to corresponding keys, ``path`` is the path to this node, ``children``
        is an iterable of child nodes constructed by ``node_factory``, optional
        ``value`` is the value associated with the path.

        ``node_factory``’s ``children`` argument is a lazy iterable which has
        a few consequences:

        * To traverse into node’s children, the object must be iterated over.
          This can be accomplished by ``children = list(children)`` statement.
        * Ignoring the argument allows ``node_factory`` to stop the traversal
          from going into the descendants of the node.  In this way, whole
          subtries can be removed from traversal.
        * If ``children`` is stored as is (i.e. as a iterator), once it is
          iterated over later on, it may see an outdated state of the trie.

        To allow constant-time determination whether the node has children, the
        ``children`` iterator implements meaningful truth value testing, such
        that ``has_children = bool(children)`` can be used.  (Note that, if the
        node has children, ``children`` value remains truthy even after the
        iterator has been exhausted).

        :func:`traverse` has two advantages over :func:`iteritems` and similar
        methods:

        1. it allows subtries to be skipped completely when going through the
           list of nodes based on the property of the parent node; and

        2. it represents structure of the trie directly making it easy to
           convert structure into a different representation.

        For example, the below snippet prints all files in current directory
        counting how many HTML files were found, but ignores hidden files and
        directories::

            import os
            import typing

            import pygtrie

            trie: pygtrie.StringTrie[bool] = pygtrie.StringTrie(
                separator=os.sep)

            # Construct a trie with all files in current directory and all of
            # its sub-directories.  Files get set a True value.  Directories are
            # represented implicitly by being prefixes of files.
            for root, _, files in os.walk('.'):
                for name in files:
                    trie[os.path.join(root, name)] = True

            def traverse_callback(
                    key_from_path: typing.Callable[[typing.Iterable[str]], str],
                    path: typing.Sequence[str],
                    children: typing.Iterable[int],
                    is_file: bool=False) -> int:
                if path and path[-1] != '.' and path[-1][0] == '.':
                    # Ignore hidden directory (but accept root node and '.')
                    return 0
                elif is_file:
                    print(key_from_path(path))
                    return int(path[-1].endswith('.html'))
                else:
                    # Otherwise, it’s a directory.  Traverse into children.
                    return sum(children)

            print(trie.traverse(traverse_callback))

        Ignoring the ``children`` argument causes subtrie to be omitted and not
        walked into.

        In the next example, the trie is converted to a tree representation
        where child nodes include a pointer to their parent.  As before, hidden
        files and directories are ignored::

            import os
            import typing

            import pygtrie

            trie: pygtrie.StringTrie[bool] = pygtrie.StringTrie(
                separator=os.sep)
            for root, _, files in os.walk('.'):
                for name in files:
                    trie[os.path.join(root, name)] = True

            class File:
                name: str
                parent: typing.Optional['File']

                def __init__(self, name: str) -> None:
                    self.name = name
                    self.parent = None

            class Directory(File):
                children: list[File]

                def __init__(self,
                             name: str,
                             children: list[File]) -> None:
                    super().__init__(name)
                    self.children = children
                    for child in children:
                        child.parent = self

            def traverse_callback(
                    key_from_path: typing.Callable[[typing.Iterable[str]], str],
                    path: typing.Sequence[str],
                    children: typing.Iterable[File | None],
                    is_regular_file: bool=False) -> File | None:
                if path and path[-1] != '.' and path[-1][0] == '.':
                    return None
                if is_regular_file:
                    return File(path[-1])
                return Directory(path[-1] if path else '',
                                 list(filter(None, children)))

            root_dir: Directory = typing.cast(
                Directory, trie.traverse(traverse_callback, prefix='.'))

        Note: Unlike iterators (e.g. returned by :func:`iteritems`), using
        ``traverse`` may raise an exception when used on a deep trie.  This may
        happen when Python’s maximum recursion depth is reached.  To address
        this, ``children`` iteration may be done non-recursively outside of the
        ``node_factory``.  For example, the below code converts a trie into an
        undirected graph using adjacency list representation::

            import collections
            import os
            import typing

            import pygtrie

            K = typing.TypeVar('K')
            V = typing.TypeVar('V')
            S = typing.TypeVar('S')

            Node = collections.namedtuple('Node', 'path neighbours')

            def undirected_graph_from_trie(
                    trie: pygtrie.Trie[K, V, S]
            ) -> list[Node]:
                '''Converts trie into a graph and returns its nodes.'''

                class Builder:
                    node: Node
                    children: typing.Iterable[typing.Self]
                    parent: Node | None

                    def __init__(
                            self,
                            key_from_path: typing.Callable[
                                [typing.Iterable[S]], K],
                            path: typing.Sequence[S],
                            children: typing.Iterable[typing.Self],
                            _: typing.Any=None) -> None:
                        self.node = Node(key_from_path(path), [])
                        self.children = children
                        self.parent = None

                    def build(self, queue: list[Node | Builder]) -> Node:
                        for builder in self.children:
                            builder.parent = self.node
                            queue.append(builder)
                        if self.parent:
                            self.parent.neighbours.append(self.node)
                            self.node.neighbours.append(self.parent)
                        return self.node

                nodes: list[Node | Builder] = [trie.traverse(Builder)]
                i = 0
                while i < len(nodes):
                    nodes[i] = typing.cast(Builder, nodes[i]).build(nodes)
                    i += 1
                return typing.cast(list[Node], nodes)

        Args:
            node_factory: Makes opaque objects from the keys and values of the
                trie.
            prefix: Prefix for node to start traversal, by default starts at
                the root.

        Returns:
            Node object constructed by node_factory corresponding to the root
            node.
        """
        node, _ = self._get_node(prefix)
        return node.traverse(node_factory,
                             self._key_from_path,
                             list(self.__path_from_key(prefix)),
                             self._items_callback)

    traverse.uses_bool_convertible_children = True  # type: ignore[attr-defined]


class CharTrie(Trie[str, V, str]):
    """:class:`Trie` which accepts and returns strings as keys.

    The only difference between :class:`CharTrie` and :class:`Trie` is that
    :class:`CharTrie` returns keys (for instance when :func:`Trie.keys` method
    is called) as strings, whereas :class:`Trie` returns keys as tuples.  For
    example, compare::

        >>> import pygtrie
        >>> trie = pygtrie.Trie()
        >>> trie['foo'] = True
        >>> trie['bar'] = True
        >>> trie.keys()
        [('f', 'o', 'o'), ('b', 'a', 'r')]

        >>> trie = pygtrie.CharTrie()
        >>> trie['foo'] = True
        >>> trie['bar'] = True
        >>> trie.keys()
        ['foo', 'bar']

    **Typing:** The class takes one generic argument ``V``.  It specifies the
    type of values stored in the trie.  The key type is ``str``.
    """

    def _key_from_path(self, path: _t.Iterable[str]) -> str:
        return ''.join(path)


class StringTrie(Trie[str, V, str]):
    """:class:`Trie` which accepts strings with a separator as keys.

    This trie accepts strings as keys which are split into components (or steps)
    using a separator specified during initialisation (forward slash by
    default).

    A common example where this class can be used is when keys are paths.  For
    example, it could map from a path to a request handler::

        import pygtrie

        def handle_root(): pass
        def handle_admin(): pass
        def handle_admin_images(): pass

        handlers = pygtrie.StringTrie()
        handlers[''] = handle_root
        handlers['/admin'] = handle_admin
        handlers['/admin/images'] = handle_admin_images

        request_path = '/admin/images/foo'

        handler = handlers.longest_prefix(request_path)

    **Typing:** The class takes one generic argument ``V``.  It specifies the
    type of values stored in the trie.  The key type is ``str``.
    """

    def __init__(self,
                 other: _Other[str, V]=(),
                 /,
                 separator: str='/',
                 **kwargs: V) -> None:
        """Initialises the trie.

        Except for a ``separator`` named argument, all other arguments are
        interpreted the same way :func:`Trie.update` interprets them.

        Args:
            other: Passed to super class initialiser.
            separator: A separator to use when splitting keys into paths used by
                the trie.
            **kwargs: Passed to super class initialiser.

        Raises:
            TypeError: If ``separator`` is not a string.
            ValueError: If ``separator`` is empty.
        """
        if not isinstance(separator, str):
            raise TypeError('separator must be a string')
        if not separator:
            raise ValueError('separator cannot be empty')
        self._separator = separator
        super().__init__(other, **kwargs)

    @_t.overload
    @classmethod
    def fromkeys(cls,
                 keys: _t.Iterable[str],
                 *,
                 separator: str='/') -> 'StringTrie[V | None]': ...
    @_t.overload
    @classmethod
    def fromkeys(cls,
                 keys: _t.Iterable[str],
                 value: V,
                 separator: str='/') -> 'StringTrie[V]': ...
    @classmethod
    def fromkeys(
            cls,
            keys: _t.Iterable[str],
            value: V | None=None,
            separator: str='/',
    ) -> _t.Union['StringTrie[V]', 'StringTrie[V | None]']:
        """Returns a new trie with given ``keys`` set to provided ``value``.

        This is equivalent to calling the constructor with a ``(key, value) for
        key in keys`` generator.

        **Typing:** Calling the method without ``value`` argument specified is
        valid only if the trie can store ``None`` values (i.e. when the trie’s
        ``V`` generic argument accepts ``None``).

        Args:
            keys: An iterable of keys that should be set in the new trie.
            value: Value to associate with given keys.  The value is not copied;
                all keys reference the same object.
            separator: A separator to use when splitting keys into paths used by
                the trie.
        """
        trie = cls(separator=separator)
        for key in keys:
            trie[key] = _t.cast(V, value)
        return trie

    @classmethod
    def _merge_impl(cls, dst: _t.Self, src: _t.Self, overwrite: bool) -> None:
        if not isinstance(dst, StringTrie):
            raise TypeError('%s cannot be merged into a %s' % (
                type(src).__name__, type(dst).__name__))
        super()._merge_impl(dst, src, overwrite=overwrite)

    def __str__(self) -> str:
        if not self:
            return '%s(separator=%s)' % (type(self).__name__, self._separator)
        return '%s(%s, separator=%s)' % (
            type(self).__name__, self._str_items(), self._separator)

    def __repr__(self) -> str:
        return '%s([%s], separator=%r)' % (
            type(self).__name__, self._str_items('(%r, %r)'), self._separator)

    def _eq_impl(self, other: _t.Self) -> bool | _types.NotImplementedType:
        # If separators differ, fall back to slow generic comparison.  This is
        # because we want StringTrie(foo/bar.baz: 42, separator=/) compare equal
        # to StringTrie(foo/bar.baz: 42, separator=.) even though they have
        # different trie structure.
        if self._separator != other._separator:  # pylint: disable=protected-access
            return NotImplemented  # type: ignore[no-any-return]
        return super()._eq_impl(other)

    def _path_from_key(self, key: str) -> _t.Sequence[str]:
        return key.split(self._separator)

    def _key_from_path(self, path: _t.Iterable[str]) -> str:
        return self._separator.join(path)


class PrefixSet(_t.Generic[K, S], _abc.MutableSet[K]):
    """A set of prefixes.

    :class:`PrefixSet` works similarly to a regular set except it contain a key
    if the key or its prefix is stored in the set.  For instance, if "foo" is
    added to the set, the set contains "foo" as well as "foobar".

    The set supports addition of elements but does *not* support removal of
    elements.  This is because there’s no obvious consistent and intuitive
    behaviour for element deletion.

    **Typing:** The class has two generic arguments: ``K`` and ``S``. They have
    the same meaning and caveats as the corresponding generic arguments of the
    :class:`Trie` class (q.v.).  To change the type of the trie backing the
    prefix set, use ``factory`` argument of the :func:`__init__` method.
    """

    def __init__(self,
                 iterable: _t.Iterable[K]=(),
                 factory: _t.Callable[..., Trie[K, _t.Literal[True], S]]=Trie,
                 **kwargs: _t.Any):
        """Initialises the prefix set.

        Args:
            iterable: A sequence of keys to add to the set.
            factory: Callback which creates the trie backing the prefix set.
            kwargs: Additional keyword arguments passed to the factory function.
                Notably necessary when using :class:`StringTrie` as the trie
                backing the prefix set.
        """
        super().__init__()
        self._trie = factory(**kwargs)
        for key in iterable:
            self.add(key)

    def copy(self) -> _t.Self:
        """Returns a shallow copy of the object."""
        cpy = self.__class__()
        cpy.__dict__ = dict(self.__dict__, _trie=self._trie.copy())
        return cpy

    def __copy__(self) -> _t.Self:
        return self.copy()

    def __deepcopy__(self, memo: _t.Any) -> _t.Self:
        cpy = self.__class__()
        cpy.__dict__ = dict(self.__dict__, _trie=self._trie.__deepcopy__(memo))
        return cpy

    def clear(self) -> None:
        """Removes all keys from the set."""
        self._trie.clear()

    def __contains__(self, key: K) -> bool:  # type: ignore[override]
        """Checks whether set contains key or its prefix."""
        return self._trie.shortest_prefix(key).get(False)

    def __iter__(self) -> _t.Iterator[K]:
        """Return iterator over all prefixes in the set.
        Cf. :func:`PrefixSet.iter`."""
        return self._trie.iterkeys()

    def iter(self, prefix: K | _Sentinel=_SENTINEL) -> _t.Iterator[K]:
        """Iterates over all keys in the set optionally starting with a prefix.

        Since a key does not have to be explicitly added to the set to be an
        element of the set, this method does not iterate over all possible keys
        that the set contains, but only over the shortest set of prefixes of all
        the keys the set contains.

        For example, if "foo" has been added to the set, the set contains also
        "foobar", but this method will *not* iterate over "foobar".

        If ``prefix`` argument is given, method will iterate over keys with
        given prefix only.  The keys yielded from the function if prefix is
        given do not have to be a subset (in the mathematical sense) of the keys
        yielded when there is no prefix.  This happens, if the set contains
        a prefix of the given prefix.

        For example, if only "foo" has been added to the set, iter method called
        with no arguments will yield "foo" only.  However, when called with
        "foobar" argument, it will yield "foobar" only.
        """
        if not _is_not_sentinel(prefix):
            return iter(self)
        if self._trie.has_node(prefix):
            return self._trie.iterkeys(prefix=prefix)
        if prefix in self:
            # Make sure the type of returned keys is consistent.
            # pylint: disable=protected-access
            key = self._trie._key_from_path(self._trie._path_from_key(prefix))
            return iter((key,))
        return iter(())

    def __len__(self) -> int:
        """Returns number of keys stored in the set.

        Since a key does not have to be explicitly added to the set to be an
        element of the set, this method does not count over all possible keys
        that the set contains (since that would be infinity), but only over the
        shortest set of prefixes of all the keys the set contains.

        For example, if "foo" has been added to the set, the set contains also
        "foobar", but this method will *not* count "foobar".
        """
        return len(self._trie)

    def add(self, value: K) -> None:
        """Adds given value to the set.

        If the set already contains prefix of the value being added, this
        operation has no effect.  If the value being added is a prefix of some
        existing values in the set, those values are deleted and replaced by
        a single entry for the value being added.

        For example, if the set contains value "foo" adding a value "foobar"
        does not change anything.  On the other hand, if the set contains values
        "foobar" and "foobaz", adding a value "foo" will replace those two
        values with a single value "foo".

        This makes a difference when iterating over the values or counting
        number of values.  Counterintuitively, adding a value can *decrease*
        size of the set.

        Args:
            value: Value to add.
        """
        # We’re friends with Trie;  pylint: disable=protected-access
        self._trie._set_node_if_no_prefix(value)

    def discard(self, value: K) -> _t.Never:
        """Raises NotImplementedError."""
        raise NotImplementedError(
            'Removing values from PrefixSet is not implemented.')

    def remove(self, value: K) -> _t.Never:
        """Raises NotImplementedError."""
        raise NotImplementedError(
            'Removing values from PrefixSet is not implemented.')

    def pop(self) -> _t.Never:
        """Raises NotImplementedError."""
        raise NotImplementedError(
            'Removing values from PrefixSet is not implemented.')
