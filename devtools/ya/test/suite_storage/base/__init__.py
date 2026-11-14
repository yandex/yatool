"""Access mutable models through a common, scoped storage contract.

Adapt the mutable storage from the suite-storage prototype. Ordinary listeners
and final reporters must observe preceding edits even when models are disk-backed;
CI's immutable working-copy store deliberately has a different contract.
"""

import abc
import collections.abc as collections_abc
import contextlib
import dataclasses
import logging
import pickle
import queue
import threading
import types
from collections.abc import Callable, Generator, Iterable, Iterator

import devtools.ya.test.suite_storage.base.metadata as suite_metadata

type SuiteId = int

logger = logging.getLogger(__name__)


@dataclasses.dataclass(frozen=True)
class Payload:
    data: bytes


@dataclasses.dataclass
class _Borrowed[T]:
    model: T
    dirty: bool = False


class _CallbackState(threading.local):
    active = False
    depth = 0


class _GuardedGenerator[T](collections_abc.Generator[T, None, None]):
    def __init__(self, iterator: Generator[T, None, None], check: Callable[[], None]) -> None:
        self._iterator = iterator
        self._check = check

    def __next__(self) -> T:
        return self.send(None)

    def send(self, value: None) -> T:
        self._check()
        return self._iterator.send(value)

    def throw(
        self,
        exception: type[BaseException] | BaseException,
        value: BaseException | None = None,
        traceback: types.TracebackType | None = None,
    ) -> T:
        self._check()
        if value is None and traceback is None:
            return self._iterator.throw(exception)
        return self._iterator.throw(exception, value, traceback)

    def close(self) -> None:
        self._check()
        self._iterator.close()


class _GuardedContext[T](contextlib.AbstractContextManager[T]):
    def __init__(self, context: contextlib.AbstractContextManager[T], check: Callable[[], None]) -> None:
        self._context = context
        self._check = check

    def __enter__(self) -> T:
        self._check()
        return self._context.__enter__()

    def __exit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, traceback: types.TracebackType | None
    ) -> bool | None:
        self._check()
        return self._context.__exit__(exc_type, exc, traceback)


class Storage[T](abc.ABC):
    def __init__(self) -> None:
        self._entries: dict[SuiteId, None] = {}
        self._next_id = 0
        self._closed = False
        self._lock = threading.RLock()
        self._stripes = tuple(threading.RLock() for _ in range(64))
        self._borrowed: dict[SuiteId, _Borrowed[T]] = {}
        self._loading: set[SuiteId] = set()
        self._callbacks = _CallbackState()
        self._metadata_factory: Callable[[T], suite_metadata.SuiteInfo] | None = None
        self._pending_releases: queue.SimpleQueue[SuiteId] = queue.SimpleQueue()
        self._release_lock = threading.Lock()
        self._release_request = object()

    def __len__(self) -> int:
        self._check_callback_access()
        with self._operation(self._lock):
            self._check_open()
            return len(self._entries)

    def ids(self) -> Iterator[SuiteId]:
        """Return stable insertion-ordered IDs without restoring models."""
        self._check_callback_access()
        with self._operation(self._lock):
            self._check_open()
            return iter(tuple(self._entries))

    def __contains__(self, suite_id: SuiteId) -> bool:
        self._check_callback_access()
        with self._operation(self._lock):
            self._check_open()
            return suite_id in self._entries

    def __iter__(self) -> Generator[T, None, None]:
        return self.mutable_iterator()

    def mutable_iterator(self, ids: Iterable[SuiteId] | None = None) -> _GuardedGenerator[T]:
        """Save each model when iteration advances or the generator closes."""
        self._check_callback_access()
        # Check before resuming a suspended loan: rejecting inside its generator
        # would unwind it and discard the consumer's unsaved changes.
        return self.guard_iterator(self._mutable_iterator(ids))

    def guard_iterator[U](self, iterator: Generator[U, None, None]) -> _GuardedGenerator[U]:
        """Keep outer collection iterators from unwinding loans inside callbacks."""
        self._check_callback_access()
        return _GuardedGenerator(iterator, self._check_callback_access)

    def guard_context[U](self, context: contextlib.AbstractContextManager[U]) -> _GuardedContext[U]:
        """Check before an adapter enters or exits its underlying context."""
        self._check_callback_access()
        return _GuardedContext(context, self._check_callback_access)

    def _mutable_iterator(self, ids: Iterable[SuiteId] | None) -> Generator[T, None, None]:
        for suite_id in self.ids() if ids is None else ids:
            with self.edit(suite_id) as model:
                yield model

    def add(self, model: T) -> SuiteId:
        """Transfer ownership of a new model to this storage."""
        self._check_callback_access()
        with self._operation(self._lock):
            self._check_open()
            suite_id = self._next_id
            with self._model_callback():
                info = self._metadata_factory(model) if self._metadata_factory is not None else None
                self._insert_model(suite_id, model, info)
            self._entries[suite_id] = None
            self._next_id += 1
            return suite_id

    def add_serialized(self, payload: bytes) -> SuiteId:
        """Import trusted pickle without deserializing it."""
        self._check_callback_access()
        with self._operation(self._lock):
            self._check_open()
            suite_id = self._next_id
            self._insert_payload(suite_id, payload)
            self._entries[suite_id] = None
            self._next_id += 1
            return suite_id

    def read(self, suite_id: SuiteId) -> contextlib.AbstractContextManager[T]:
        """Borrow a model without permission to modify or retain it."""
        self._check_callback_access()
        return self.guard_context(self._access(suite_id, writable=False))

    def edit(self, suite_id: SuiteId) -> contextlib.AbstractContextManager[T]:
        """Persist edits on scope exit, including exception unwinding; no rollback."""
        self._check_callback_access()
        return self.guard_context(self._access(suite_id, writable=True))

    def replace(self, suite_id: SuiteId, model: T) -> None:
        """Replace a model without changing its stable ID."""
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            with self._lock:
                self._check_id(suite_id)
                borrowed = self._borrowed.get(suite_id)
                if borrowed is not None:
                    borrowed.model = model
                    borrowed.dirty = True
                else:
                    self._save_model(suite_id, model)

    def serialized(self, suite_id: SuiteId) -> bytes:
        """Return current pickle, including an enclosing edit's changes."""
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            with self._lock:
                self._check_id(suite_id)
                borrowed = self._borrowed.get(suite_id)
                if borrowed is None:
                    with self._model_callback():
                        return self._read_payload(suite_id)
                model = borrowed.model
            with self._model_callback():
                return pickle.dumps(model, protocol=pickle.HIGHEST_PROTOCOL)

    def metadata(
        self,
        suite_id: SuiteId,
        factory: Callable[[T], suite_metadata.SuiteInfo],
        *,
        serialized_payload: bytes | None = None,
    ) -> suite_metadata.SuiteInfo:
        """Read indexed fields, restoring a model only on a miss.

        A single-producer importer may supply the exact newly registered payload
        before exposing the record to readers, avoiding an immediate spool reread.
        """
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            with self._lock:
                self._check_id(suite_id)
                if self._metadata_factory is None:
                    self._metadata_factory = factory
                elif self._metadata_factory is not factory:
                    raise ValueError('A suite storage must use one metadata extractor')
                borrowed = self._borrowed.get(suite_id)
                if borrowed is None:
                    cached = self._read_metadata(suite_id)
                    if cached is not None:
                        return cached
            if borrowed is not None:
                with self._model_callback():
                    return factory(borrowed.model)
            if serialized_payload is not None:
                with self._lock:
                    self._loading.add(suite_id)
                try:
                    with self._model_callback():
                        info = factory(pickle.loads(serialized_payload, encoding='utf-8'))
                    with self._lock:
                        self._cache_metadata(suite_id, info)
                    return info
                finally:
                    with self._lock:
                        self._loading.remove(suite_id)
            with self.read(suite_id) as model:
                with self._model_callback():
                    info = factory(model)
            with self._lock:
                self._cache_metadata(suite_id, info)
            return info

    def snapshot(
        self, suite_id: SuiteId, factory: Callable[[T], suite_metadata.SuiteInfo]
    ) -> tuple[suite_metadata.SuiteInfo, bytes]:
        """Export metadata and payload from the same record version."""
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            return self.metadata(suite_id, factory), self.serialized(suite_id)

    def discard(self, suite_id: SuiteId) -> None:
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            with self._lock:
                self._check_id(suite_id)
                if suite_id in self._borrowed or suite_id in self._loading:
                    raise RuntimeError('Cannot discard a borrowed suite')
                self._delete(suite_id)
                del self._entries[suite_id]

    def release(self, suite_id: SuiteId) -> None:
        """Release an unreferenced selection entry, also after owner cleanup."""
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            with self._lock:
                if not self._closed and suite_id in self._entries:
                    if suite_id in self._borrowed or suite_id in self._loading:
                        raise RuntimeError('Cannot release a borrowed suite')
                    self._delete(suite_id)
                    del self._entries[suite_id]

    def defer_release(self, suite_id: SuiteId) -> None:
        """Queue finalizer cleanup without reentry or blocking on another loan."""
        if not self._closed:
            # SimpleQueue.put is reentrant in CPython, including from finalizers.
            self._pending_releases.put(suite_id)
            self._drain_releases()

    def _drain_releases(self) -> None:
        if self._callbacks.active or self._callbacks.depth:
            return
        # A failed try-lock must still notify the current drainer. Unique markers
        # coalesce requests without another lock or a counter read/modify/write.
        self._release_request = object()
        while self._release_lock.acquire(blocking=False):
            request = self._release_request
            try:
                self._drain_release_batch()
            finally:
                self._release_lock.release()
            # Check after unlock: a concurrent requester either changes this
            # marker or acquires the now-free lock and takes over the cleanup.
            if self._release_request is request:
                return

    def _drain_release_batch(self) -> None:
        retry = []
        try:
            while True:
                try:
                    suite_id = self._pending_releases.get_nowait()
                except queue.Empty:
                    break
                try:
                    released = self._try_release(suite_id)
                except Exception:
                    # Cleanup is independent of the operation that scheduled it.
                    # Never turn a published add into an exception with no owner.
                    retry.append(suite_id)
                    logger.warning('Deferred release of suite %s failed; keeping it pending', suite_id, exc_info=True)
                    continue
                except BaseException:
                    retry.append(suite_id)
                    raise
                if not released:
                    retry.append(suite_id)
        finally:
            for suite_id in retry:
                self._pending_releases.put(suite_id)

    def _try_release(self, suite_id: SuiteId) -> bool:
        # Never introduce a cross-ID wait from GC or while finishing an operation.
        stripe = self._stripes[suite_id % len(self._stripes)]
        if not stripe.acquire(blocking=False):
            return False
        try:
            if not self._lock.acquire(blocking=False):
                return False
            try:
                if self._closed or suite_id not in self._entries:
                    return True
                if suite_id in self._borrowed or suite_id in self._loading:
                    return False
                self._delete(suite_id)
                del self._entries[suite_id]
                return True
            finally:
                self._lock.release()
        finally:
            stripe.release()

    @contextlib.contextmanager
    def _operation(self, lock: contextlib.AbstractContextManager[object]) -> Iterator[None]:
        self._callbacks.depth += 1
        try:
            with lock:
                yield
        finally:
            self._callbacks.depth -= 1
            # Delete only after publication/loading bookkeeping and locks settle.
            self._drain_releases()

    def close(self) -> None:
        self._check_callback_access()
        with self._operation(self._lock):
            if self._closed:
                return
            if self._borrowed or self._loading:
                raise RuntimeError('Cannot close storage with borrowed suites')
            self._close()
            self._entries.clear()
            self._closed = True

    def __enter__(self) -> 'Storage[T]':
        self._check_callback_access()
        self._check_open()
        return self

    def __exit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, traceback: types.TracebackType | None
    ) -> None:
        self.close()

    @contextlib.contextmanager
    def _access(self, suite_id: SuiteId, writable: bool) -> Iterator[T]:
        # A stripe serializes access to this ID, including same-thread nesting.
        # Deserialization and consumer code never hold the global file/state lock.
        self._check_callback_access()
        with self._operation(self._stripes[suite_id % len(self._stripes)]):
            with self._lock:
                self._check_id(suite_id)
                borrowed = self._borrowed.get(suite_id)
                outermost = borrowed is None
                if outermost:
                    model = self._read_model(suite_id)
                    self._loading.add(suite_id)
                else:
                    model = borrowed.model
            if outermost:
                try:
                    if isinstance(model, Payload):
                        with self._model_callback():
                            model = pickle.loads(model.data, encoding='utf-8')
                    borrowed = _Borrowed(model)
                    with self._lock:
                        self._borrowed[suite_id] = borrowed
                finally:
                    with self._lock:
                        self._loading.remove(suite_id)
            assert borrowed is not None
            borrowed.dirty |= writable
            try:
                yield borrowed.model
            finally:
                if outermost:
                    try:
                        if borrowed.dirty:
                            self._save_model(suite_id, borrowed.model)
                    finally:
                        with self._lock:
                            del self._borrowed[suite_id]

    def _check_open(self) -> None:
        if self._closed:
            raise ValueError('Suite storage is closed')

    def _check_id(self, suite_id: SuiteId) -> None:
        self._check_open()
        if suite_id not in self._entries:
            raise KeyError(suite_id)

    def _save_model(self, suite_id: SuiteId, model: T) -> None:
        with self._model_callback():
            info = self._metadata_factory(model) if self._metadata_factory is not None else None
            self._write_model(suite_id, model, info)

    def _check_callback_access(self) -> None:
        if self._callbacks.active:
            raise RuntimeError('Cannot access suite storage from a model callback')

    @contextlib.contextmanager
    def _model_callback(self) -> Iterator[None]:
        # Pickle hooks and metadata extractors must not change a snapshot while
        # it is being prepared, reuse an insertion ID, or invert lock order.
        # Reject reentry before acquiring locks; ordinary nested loans remain valid.
        self._check_callback_access()
        self._callbacks.active = True
        try:
            yield
        finally:
            self._callbacks.active = False

    @abc.abstractmethod
    def _insert_model(self, suite_id: SuiteId, model: T, info: suite_metadata.SuiteInfo | None = None) -> None: ...

    @abc.abstractmethod
    def _insert_payload(self, suite_id: SuiteId, payload: bytes) -> None: ...

    @abc.abstractmethod
    def _read_model(self, suite_id: SuiteId) -> T | Payload: ...

    @abc.abstractmethod
    def _write_model(self, suite_id: SuiteId, model: T, info: suite_metadata.SuiteInfo | None = None) -> None: ...

    @abc.abstractmethod
    def _read_metadata(self, suite_id: SuiteId) -> suite_metadata.SuiteInfo | None: ...

    @abc.abstractmethod
    def _cache_metadata(self, suite_id: SuiteId, info: suite_metadata.SuiteInfo) -> None: ...

    @abc.abstractmethod
    def _read_payload(self, suite_id: SuiteId) -> bytes: ...

    @abc.abstractmethod
    def _delete(self, suite_id: SuiteId) -> None: ...

    @abc.abstractmethod
    def _close(self) -> None: ...
