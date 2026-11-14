"""Share borrowed container views and pure suite adapters without import cycles.

Recursive freezing and model adapters live together so nested model attributes
need neither a reverse module dependency nor an import on the read hot path.
"""

import collections.abc
import copy
import dataclasses
import typing
from collections.abc import Iterable, Iterator, Mapping, Sequence, Set

import devtools.ya.test.common as test_common
import devtools.ya.test.facility.containers as containers
import devtools.ya.test.facility.testcase as testcase
import devtools.ya.test.test_types.common as suite_types

type Scalar = None | bool | int | float | complex | str | bytes
type Value = Scalar | ConstView[object] | ConstMapping | ConstSequence | ConstSet
type Suite = suite_types.AbstractTestSuite | suite_types.SkippedTestSuite


@dataclasses.dataclass(frozen=True, slots=True)
class ConstView[T]:
    _value: T

    def __eq__(self, other: object) -> bool:
        return self._value == (other._value if isinstance(other, ConstView) else other)

    def __hash__(self) -> int:
        return hash(self._value)

    def __getattr__(self, name: str) -> Value:
        if name.startswith('_'):
            raise AttributeError(name)
        value = getattr(self._value, name)
        if callable(value):
            raise AttributeError(f'Method {name!r} is not available on a read-only view')
        return freeze(value)


@dataclasses.dataclass(frozen=True, slots=True, eq=False)
class ConstMapping(collections.abc.Mapping[object, Value]):
    _value: Mapping[object, object]

    def __len__(self) -> int:
        return len(self._value)

    def __iter__(self) -> Iterator[object]:
        for key in self._value:
            if not _immutable_key(key):
                raise TypeError('Read-only mappings require immutable keys')
            yield key

    def __getitem__(self, key: object) -> Value:
        # A defaultdict's __missing__ would otherwise mutate through a read.
        if key not in self._value:
            raise KeyError(key)
        return freeze(self._value[key])

    def __deepcopy__(self, memo: dict) -> dict:
        # Report records own plain data; they must not retain borrowed wrappers.
        return copy.deepcopy(dict(self._value), memo)


@dataclasses.dataclass(frozen=True, slots=True)
class ConstSequence(collections.abc.Sequence[Value]):
    _value: Sequence[object]

    def __len__(self) -> int:
        return len(self._value)

    @typing.overload
    def __getitem__(self, index: int) -> Value: ...

    @typing.overload
    def __getitem__(self, index: slice) -> 'ConstSequence': ...

    def __getitem__(self, index: int | slice) -> Value:
        return ConstSequence(self._value[index]) if isinstance(index, slice) else freeze(self._value[index])

    def __iter__(self) -> Iterator[Value]:
        for value in self._value:
            yield freeze(value)

    def __deepcopy__(self, memo: dict) -> list | tuple:
        return copy.deepcopy(self._value, memo)

    def __eq__(self, other: object) -> bool:
        return self._value == _unwrap(other)

    def __hash__(self) -> int:
        return hash(self._value)


@dataclasses.dataclass(frozen=True, slots=True, eq=False)
class ConstSet(collections.abc.Set[Value]):
    _value: Set[object]

    @classmethod
    def _from_iterable(cls, values: Iterable[object]) -> 'ConstSet':
        # ABC mixins pass one-shot generators containing borrowed wrappers.
        return cls(frozenset(_unwrap(value) for value in values))

    def __len__(self) -> int:
        return len(self._value)

    def __contains__(self, value: object) -> bool:
        return _unwrap(value) in self._value

    def __hash__(self) -> int:
        return hash(self._value)

    def __le__(self, other: object) -> bool:
        if not isinstance(other, collections.abc.Set):
            return NotImplemented
        other = _unwrap(other)
        return len(self) <= len(other) and all(value in other for value in self._value)

    def __sub__(self, other: object) -> 'ConstSet':
        if not isinstance(other, collections.abc.Set):
            if not isinstance(other, collections.abc.Iterable):
                return NotImplemented
            other = self._from_iterable(other)
        other = _unwrap(other)
        return self._from_iterable(value for value in self._value if value not in other)

    def __rsub__(self, other: object) -> 'ConstSet':
        if not isinstance(other, collections.abc.Iterable):
            return NotImplemented
        return self._from_iterable(value for value in other if value not in self)

    def __iter__(self) -> Iterator[Value]:
        for value in self._value:
            yield freeze(value)

    def __deepcopy__(self, memo: dict) -> set | frozenset:
        return copy.deepcopy(self._value, memo)


def _immutable_key(value: object) -> bool:
    if value is None or isinstance(value, (bool, int, float, complex, str, bytes)):
        return True
    return isinstance(value, (tuple, frozenset)) and all(_immutable_key(item) for item in value)


def _unwrap(value: object) -> object:
    if isinstance(value, (ConstView, ConstMapping, ConstSequence, ConstSet)):
        return value._value
    return value


def freeze(value: object) -> Value:
    if value is None or isinstance(value, (bool, int, float, complex, str, bytes)):
        return value
    if isinstance(value, (ConstView, ConstMapping, ConstSequence, ConstSet)):
        return value
    if isinstance(value, collections.abc.Mapping):
        return ConstMapping(value)
    if isinstance(value, collections.abc.Sequence):
        return ConstSequence(value)
    if isinstance(value, collections.abc.Set):
        return ConstSet(value)
    adapted = adapt(value)
    return adapted if adapted is not None else ConstView(value)


class ContainerView[T: containers.Container | suite_types.SkippedTestSuite](ConstView[T]):
    __slots__ = ()

    def get_comment(self) -> str:
        return self._value.get_comment()

    def has_comment(self) -> bool:
        return self._value.has_comment()

    def get_errors(self) -> tuple[tuple[int, str], ...]:
        return tuple(self._value.get_errors())

    def get_info(self) -> tuple[tuple[int, str], ...]:
        return tuple(self._value.get_info())


class ChunkView(ContainerView[containers.Chunk]):
    __slots__ = ()

    def get_name(self) -> str:
        return self._value.get_name()

    def get_status(self) -> int:
        return self._value.get_status()


class TestCaseView(ConstView[testcase.TestCase]):
    __slots__ = ()

    def get_class_name(self) -> str:
        return self._value.get_class_name()

    def get_test_case_name(self) -> str:
        return self._value.get_test_case_name()


class SuiteView(ContainerView[Suite]):
    __slots__ = ()

    def get_status(self, relaxed: bool = False) -> int:
        return self._value.get_status(relaxed=relaxed)

    def is_skipped(self) -> bool:
        return self._value.is_skipped()

    def get_type(self) -> str:
        return self._value.get_type()

    def get_ci_type_name(self) -> str:
        return self._value.get_ci_type_name()

    def work_dir(self, *path: str) -> str:
        directory = self._value._work_dir
        if directory is None:
            directory = test_common.get_test_suite_work_dir(
                '$(BUILD_ROOT)',
                self._value.project_path,
                self._value.name,
                target_platform_descriptor=self._value.target_platform_descriptor,
                multi_target_platform_run=self._value.multi_target_platform_run,
            )
        return directory + '/' + '/'.join(path) if path else directory

    @property
    def _original_requirements(self) -> ConstMapping | None:
        requirements = self._value._original_requirements
        return ConstMapping(requirements) if requirements is not None else None

    @property
    def tests(self) -> '_SuiteCases':
        return _SuiteCases(self._value)


class _SuiteCases(collections.abc.Collection[TestCaseView]):
    __slots__ = ('_suite',)

    def __init__(self, suite: Suite) -> None:
        object.__setattr__(self, '_suite', suite)

    def __setattr__(self, name: str, value: object) -> None:
        raise AttributeError('Read-only test-case collection')

    def __len__(self) -> int:
        return sum(len(chunk.tests) for chunk in self._suite.chunks)

    def __iter__(self) -> Iterator[TestCaseView]:
        for chunk in self._suite.chunks:
            for case in chunk.tests:
                yield TestCaseView(case)

    def __contains__(self, value: object) -> bool:
        return any(case == value for case in self)


def adapt(value: object) -> ConstView[object] | None:
    if isinstance(value, (suite_types.AbstractTestSuite, suite_types.SkippedTestSuite)):
        return SuiteView(value)
    if isinstance(value, containers.Chunk):
        return ChunkView(value)
    if isinstance(value, testcase.TestCase):
        return TestCaseView(value)
    return None
