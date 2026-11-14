"""Keep mutable suite snapshots on disk, with lazily indexed metadata."""

import array
import os
import pickle
import tempfile

import devtools.ya.test.suite_storage.base as base
import devtools.ya.test.suite_storage.base.metadata as metadata
import devtools.ya.test.suite_storage.disk.index as index


class DiskStorage[T](base.Storage[T]):
    def __init__(self, directory: str | None = None) -> None:
        super().__init__()
        # Adapt CI's raw-payload/uint64 addressing without retaining models.
        # Mutable readers need current snapshots, unlike CI's immutable source.
        self._file = tempfile.TemporaryFile(mode='w+b', dir=directory)
        self._offsets = array.array('Q')
        self._lengths = array.array('Q')
        self._directory = directory
        self._index: index.SQLiteIndex | None = None

    def _insert_model(self, suite_id: base.SuiteId, model: T, info: metadata.SuiteInfo | None = None) -> None:
        payload = pickle.dumps(model, protocol=pickle.HIGHEST_PROTOCOL)
        with self._lock:
            self._file.seek(0, os.SEEK_END)
            offset = self._file.tell()
            self._file.write(payload)
            if info is not None:
                self._cache_metadata(suite_id, info)
            self._offsets.append(offset)
            self._lengths.append(len(payload))

    def _insert_payload(self, suite_id: base.SuiteId, payload: bytes) -> None:
        with self._lock:
            self._file.seek(0, os.SEEK_END)
            offset = self._file.tell()
            self._file.write(payload)
            self._offsets.append(offset)
            self._lengths.append(len(payload))

    def _read_model(self, suite_id: base.SuiteId) -> base.Payload:
        return base.Payload(self._read_payload(suite_id))

    def _write_model(self, suite_id: base.SuiteId, model: T, info: metadata.SuiteInfo | None = None) -> None:
        payload = pickle.dumps(model, protocol=pickle.HIGHEST_PROTOCOL)
        with self._lock:
            self._file.seek(0, os.SEEK_END)
            offset = self._file.tell()
            self._file.write(payload)
            if info is not None:
                # Publish addressing only after SQL succeeds. A failed update
                # retains the previous payload and index, leaving unused bytes.
                self._cache_metadata(suite_id, info)
            self._offsets[suite_id] = offset
            self._lengths[suite_id] = len(payload)

    def _read_metadata(self, suite_id: base.SuiteId) -> metadata.SuiteInfo | None:
        return self._index.get(suite_id) if self._index is not None else None

    def _cache_metadata(self, suite_id: base.SuiteId, info: metadata.SuiteInfo) -> None:
        if self._index is None:
            self._index = index.SQLiteIndex(self._directory)
        self._index.put(suite_id, info)

    def _read_payload(self, suite_id: base.SuiteId) -> bytes:
        with self._lock:
            self._file.seek(self._offsets[suite_id])
            return self._file.read(self._lengths[suite_id])

    def _delete(self, suite_id: base.SuiteId) -> None:
        if self._index is not None:
            self._index.delete(suite_id)
        self._offsets[suite_id] = 0
        self._lengths[suite_id] = 0

    def _close(self) -> None:
        self._file.close()
        if self._index is not None:
            self._index.close()
        self._offsets = array.array('Q')
        self._lengths = array.array('Q')
