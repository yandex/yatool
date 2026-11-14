"""Share model ownership across ordered selections and result consumers.

Adapt the suite-storage prototype's reference selections so ordinary clients can
use the common mutable backend contract without retaining every restored model.
"""

import collections.abc
import contextlib
import dataclasses
import heapq
import typing
import weakref
from collections.abc import Callable, Generator, Iterable, Iterator

import devtools.ya.test.suite_storage.base as base
import devtools.ya.test.suite_storage.base.metadata as suite_metadata
import devtools.ya.test.suite_storage.in_memory as in_memory
import devtools.ya.test.suite_storage.suite_views as suite_views
import devtools.ya.test.test_types.common as suite_types

type Suite = suite_types.AbstractTestSuite | suite_types.SkippedTestSuite


@dataclasses.dataclass(eq=False, slots=True, weakref_slot=True)
class SuiteRef:
    storage: base.Storage[Suite]
    suite_id: base.SuiteId

    def __post_init__(self) -> None:
        weakref.finalize(self, self.storage.defer_release, self.suite_id)

    @property
    def uid(self) -> str | None:
        # Metadata sees nested edits and resident aliases; a ref-local cache cannot.
        return self.storage.metadata(self.suite_id, _info).uid


class _SlowSuite(typing.NamedTuple):
    project_path: str
    suite_type: str
    target_platform_descriptor: str | None
    multi_target_platform_run: bool

    def get_type(self) -> str:
        return self.suite_type


class _SlowCase(typing.NamedTuple):
    name: str
    status: int
    elapsed: float


def _info(suite: Suite) -> suite_metadata.SuiteInfo:
    return suite_metadata.SuiteInfo(
        suite.uid,
        suite.project_path,
        suite.name,
        suite.target_platform_descriptor,
        suite.test_size,
        suite.get_ci_type_name(),
        suite.is_skipped(),
        tuple(suite.result_uids),
        tuple(suite.output_uids),
    )


class Suites:
    """Own ordered references; iteration saves each mutable model on advance.

    Retained selections must contain refs, not list(suites). Use explicit scopes
    for asynchronous processing or early exit; models cannot escape a loan.
    """

    def __init__(self, storage: base.Storage[Suite] | None = None) -> None:
        self._storage = storage if storage is not None else in_memory.InMemoryStorage[Suite]()
        self._refs: list[SuiteRef] = []
        self._owners = {self._storage}

    def add(self, suite: Suite) -> SuiteRef:
        ref = SuiteRef(self._storage, self._storage.add(suite))
        self._refs.append(ref)
        return ref

    @classmethod
    def from_models(cls, models: Iterable[Suite]) -> 'Suites':
        if isinstance(models, cls):
            return models
        result = cls()
        for suite in models:
            result.add(suite)
        return result

    @classmethod
    def from_serialized(cls, payloads: dict[str, str | None], storage: base.Storage[Suite] | None = None) -> 'Suites':
        """Consume trusted context pickle without expanding all models at once."""
        result = cls(storage)
        try:
            for uid in payloads:
                payload = payloads[uid]
                if payload is None:
                    raise ValueError(f'Missing serialized suite {uid}')
                ref = SuiteRef(result._storage, result._storage.add_serialized(payload.encode('latin-1')))
                result._refs.append(ref)
                payloads[uid] = None
            payloads.clear()
        except BaseException:
            result.close()
            raise
        return result

    def register_serialized(self, uid: str, payload: bytes) -> SuiteRef:
        """Register an unattached record for an ordered streaming selection."""
        return SuiteRef(self._storage, self._storage.add_serialized(payload))

    def metadata_for(self, ref: SuiteRef, *, serialized_payload: bytes | None = None) -> suite_metadata.SuiteInfo:
        """Read or initialize metadata for one record without a collection walk."""
        return ref.storage.metadata(ref.suite_id, _info, serialized_payload=serialized_payload)

    def export(self) -> Iterator[tuple[str, bytes]]:
        for ref in self._refs:
            info, payload = ref.storage.snapshot(ref.suite_id, _info)
            uid = info.uid
            if uid is None:
                raise ValueError('Cannot export an unprepared suite without UID')
            yield uid, payload

    def serialized(self) -> dict[str, str]:
        return {uid: payload.decode('latin-1') for uid, payload in self.export()}

    def __len__(self) -> int:
        return len(self._refs)

    def __bool__(self) -> bool:
        return bool(self._refs)

    def refs(self) -> Iterator[SuiteRef]:
        return iter(self._refs)

    def __iter__(self) -> Generator[Suite, None, None]:
        return self.mutable_iterator()

    def mutable_iterator(self) -> Generator[Suite, None, None]:
        return self._guard_iterator(self._mutable_iterator())

    def _mutable_iterator(self) -> Generator[Suite, None, None]:
        for ref in self._refs:
            with self.edit(ref) as suite:
                yield suite

    def const_iterator(self) -> Generator[suite_views.SuiteView, None, None]:
        """Borrow pure read views without serializing models or updating indexes."""
        return self._guard_iterator(self._const_iterator())

    def _const_iterator(self) -> Generator[suite_views.SuiteView, None, None]:
        for ref in self._refs:
            with self.read(ref) as suite:
                yield suite_views.SuiteView(suite)

    def report_iterator(self) -> Generator[Suite | suite_views.SuiteView, None, None]:
        """Use const reads, except legacy skipped descriptors needing a new UID."""
        return self._guard_iterator(self._report_iterator())

    def _report_iterator(self) -> Generator[Suite | suite_views.SuiteView, None, None]:
        for ref in self._refs:
            with self.read(ref) as suite:
                needs_uid = suite.uid is None and suite.is_skipped()
                if not needs_uid:
                    yield suite_views.SuiteView(suite)
            if needs_uid:
                # The legacy prototype assigns this UID. Preserve that edit for
                # subsequent consumers rather than mutating a const view.
                with self.edit(ref) as suite:
                    yield suite

    def _guard_iterator[U](self, iterator: Generator[U, None, None]) -> Generator[U, None, None]:
        for storage in self._owners:
            iterator = storage.guard_iterator(iterator)
        return iterator

    def edit(self, ref: SuiteRef) -> contextlib.AbstractContextManager[Suite]:
        return ref.storage.guard_context(self._edit(ref))

    @contextlib.contextmanager
    def _edit(self, ref: SuiteRef) -> Iterator[Suite]:
        with ref.storage.edit(ref.suite_id) as suite:
            yield suite

    def read(self, ref: SuiteRef) -> contextlib.AbstractContextManager[Suite]:
        return ref.storage.guard_context(self._read(ref))

    @contextlib.contextmanager
    def _read(self, ref: SuiteRef) -> Iterator[Suite]:
        with ref.storage.read(ref.suite_id) as suite:
            yield suite

    def replace(self, ref: SuiteRef, suite: Suite) -> None:
        ref.storage.replace(ref.suite_id, suite)

    def metadata(self) -> Iterator[suite_metadata.SuiteInfo]:
        for ref in self._refs:
            yield ref.storage.metadata(ref.suite_id, _info)

    def by_uid(self) -> dict[str, SuiteRef]:
        result: dict[str, SuiteRef] = {}
        for ref in self._refs:
            uid = ref.uid
            if uid is not None:
                result[uid] = ref
        return result

    def select(self, predicate: Callable[[Suite], bool], *, writable: bool = False) -> 'Suites':
        refs = []
        for ref in self._refs:
            with self.edit(ref) if writable else self.read(ref) as suite:
                if predicate(suite):
                    refs.append(ref)
        return self.select_refs(refs)

    def select_refs(self, refs: Iterable[SuiteRef]) -> 'Suites':
        result = Suites(self._storage)
        result._refs = list(refs)
        result._owners = self._owners | {ref.storage for ref in result._refs}
        return result

    def __add__(self, other: Iterable[Suite]) -> 'Suites':
        selection = Suites.from_models(other)
        return self.select_refs(self._refs + selection._refs)

    def close(self) -> None:
        for storage in self._owners:
            storage.close()

    def slowest_test_cases(self, limit: int) -> list[tuple[_SlowSuite, _SlowCase]]:
        if limit <= 0:
            return []
        return heapq.nsmallest(
            limit, self._case_times(), key=lambda pair: (-pair[1].elapsed, pair[0].project_path, pair[1].name)
        )

    def _case_times(self) -> Iterator[tuple[_SlowSuite, _SlowCase]]:
        for ref in self._refs:
            with self.read(ref) as suite:
                info = _SlowSuite(
                    suite.project_path,
                    suite.get_type(),
                    suite.target_platform_descriptor,
                    suite.multi_target_platform_run,
                )
                for chunk in suite.chunks:
                    for case in chunk.tests:
                        if case.elapsed > 0:
                            yield info, _SlowCase(case.name, case.status, case.elapsed)


def metadata(suites: Suites | Iterable[Suite]) -> Iterable[suite_metadata.SuiteInfo | Suite]:
    """Keep caller-owned fixtures compatible at reporting boundaries."""
    return _Metadata(suites) if isinstance(suites, Suites) else suites


def close_collections(*selections: object) -> None:
    """Release mutable storage after all reporting and diagnostic consumers."""
    with contextlib.ExitStack() as cleanup:
        for selection in selections:
            if isinstance(selection, Suites):
                cleanup.callback(selection.close)


class _Metadata(collections.abc.Collection[suite_metadata.SuiteInfo]):
    def __init__(self, suites: Suites) -> None:
        self._suites = suites

    def __len__(self) -> int:
        return len(self._suites)

    def __iter__(self) -> Iterator[suite_metadata.SuiteInfo]:
        return self._suites.metadata()

    def __contains__(self, value: object) -> bool:
        return any(item == value for item in self)
