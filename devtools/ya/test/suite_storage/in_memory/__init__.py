"""Keep mutable models resident and imported payloads lazy between loans."""

import pickle

import devtools.ya.test.suite_storage.base as base
import devtools.ya.test.suite_storage.base.metadata as metadata


class InMemoryStorage[T](base.Storage[T]):
    def __init__(self) -> None:
        super().__init__()
        self._records: dict[base.SuiteId, T | base.Payload] = {}
        self._serialized_ids: set[base.SuiteId] = set()
        self._metadata: dict[base.SuiteId, metadata.SuiteInfo] = {}

    def _insert_model(self, suite_id: base.SuiteId, model: T, info: metadata.SuiteInfo | None = None) -> None:
        self._records[suite_id] = model

    def _insert_payload(self, suite_id: base.SuiteId, payload: bytes) -> None:
        self._records[suite_id] = base.Payload(payload)
        self._serialized_ids.add(suite_id)

    def _read_model(self, suite_id: base.SuiteId) -> T | base.Payload:
        return self._records[suite_id]

    def _write_model(self, suite_id: base.SuiteId, model: T, info: metadata.SuiteInfo | None = None) -> None:
        record = (
            base.Payload(pickle.dumps(model, protocol=pickle.HIGHEST_PROTOCOL))
            if suite_id in self._serialized_ids
            else model
        )
        with self._lock:
            if info is not None:
                self._cache_metadata(suite_id, info)
            self._records[suite_id] = record

    def _read_metadata(self, suite_id: base.SuiteId) -> metadata.SuiteInfo | None:
        # Legacy aliases can mutate resident models without a scope. Only cache
        # metadata of imported records, which are disposable between loans.
        return self._metadata.get(suite_id) if suite_id in self._serialized_ids else None

    def _cache_metadata(self, suite_id: base.SuiteId, info: metadata.SuiteInfo) -> None:
        if suite_id in self._serialized_ids:
            self._metadata[suite_id] = info

    def _read_payload(self, suite_id: base.SuiteId) -> bytes:
        record = self._records[suite_id]
        return (
            record.data if isinstance(record, base.Payload) else pickle.dumps(record, protocol=pickle.HIGHEST_PROTOCOL)
        )

    def _delete(self, suite_id: base.SuiteId) -> None:
        del self._records[suite_id]
        self._serialized_ids.discard(suite_id)
        self._metadata.pop(suite_id, None)

    def _close(self) -> None:
        self._records.clear()
        self._serialized_ids.clear()
        self._metadata.clear()
