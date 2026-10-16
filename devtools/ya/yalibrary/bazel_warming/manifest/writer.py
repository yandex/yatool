"""Снимок плана до HEAD/strip и итоги PUT до освобождения build root."""

from dataclasses import replace
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import threading

from .model import Manifest, ManifestError, WrittenFile, relative_output
from .fileio import encoded_stream

# -----------------------------------------------------------------------------
# Атомарная запись приватного файла
# -----------------------------------------------------------------------------


def write_manifest(path, manifest):
    """Атомарно записать приватный JSON; суффикс .gz включает сжатие."""
    path = Path(path)
    data = manifest.to_dict()
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    descriptor, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as raw:
            with encoded_stream(raw, path.suffix == '.gz') as destination:
                output = io.TextIOWrapper(destination, encoding='utf-8')
                try:
                    json.dump(data, output, ensure_ascii=False, indent=2, allow_nan=False)
                    output.write('\n')
                finally:
                    output.detach()  # flush, но gzip trailer должен записаться до fsync raw.
            raw.flush()
            os.fsync(raw.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


# -----------------------------------------------------------------------------
# Один writer на сборку; обращения upload workers сериализуются
# -----------------------------------------------------------------------------


class ManifestWriter:
    def __init__(self, path, run_id, max_file_size=0):
        """Сразу заменить старый результат незавершённым документом нового запуска."""
        self.path = Path(path).absolute()
        self.run_id = run_id
        self.max_file_size = max_file_size
        self._lock = threading.Lock()
        self._nodes = {}
        self._selected = False
        self._finished = False
        self._error = None
        write_manifest(self.path, self._snapshot())

    def _snapshot(self, complete=False, build_exit_code=None):
        return Manifest(
            run_id=self.run_id,
            nodes=tuple(self._nodes.values()),
            max_file_size=self.max_file_size,
            complete=complete,
            build_exit_code=build_exit_code,
        )

    def select(self, nodes):
        """Сохранить исходные выбранные UID ровно один раз, до удаления найденного AC."""
        with self._lock:
            if self._selected or self._finished:
                raise ManifestError('План прогрева уже зафиксирован.')
            snapshot = Manifest(self.run_id, tuple(nodes), self.max_file_size)
            snapshot.validate()
            if any(node.put_status != 'not_attempted' for node in snapshot.nodes):
                raise ManifestError('Начальный план не должен содержать итоги PUT.')
            self._nodes = {node.uid: node for node in snapshot.nodes}
            try:
                write_manifest(self.path, snapshot)
            except Exception as error:
                self._error = error
                raise
            self._selected = True

    def record_put(self, uid, status, root_dir, files):
        """Сохранить реальный размер каждого выхода, пока upload task держит build root."""
        with self._lock:
            if not self._selected or self._finished:
                raise ManifestError('Итог PUT получен вне исполнения зафиксированного плана.')
            if uid not in self._nodes:
                return  # Зависимости вне выбранных результатов не расширяют план.
            try:
                if status not in ('ok', 'failed', 'skipped'):
                    raise ManifestError(f'Неизвестный статус PUT: {status!r}.')
                root = Path(root_dir).absolute()
                resolved_root = root.resolve(strict=True)
                written = []
                for filename in sorted(files):
                    path = Path(filename).absolute()
                    try:
                        relative = path.relative_to(root).as_posix()
                        path.resolve(strict=True).relative_to(resolved_root)
                    except ValueError as error:
                        raise ManifestError('Выход PUT находится вне build root.') from error
                    info = path.stat()
                    if not stat.S_ISREG(info.st_mode):
                        raise ManifestError('Выход PUT должен быть обычным файлом.')
                    written.append(WrittenFile(relative_output(relative), info.st_size))
                node = replace(
                    self._nodes[uid],
                    put_status='skipped_size' if status == 'skipped' else status,
                    written_files=tuple(written),
                )
                node.validate(self.max_file_size)
                self._nodes[uid] = node
            except Exception as error:
                self._error = error
                raise

    def finish(self, build_exit_code):
        """Завершить manifest после ожидания runner; ни один потерянный итог не допускается."""
        with self._lock:
            if not self._selected or self._finished:
                raise ManifestError('Нельзя завершить незафиксированный или уже завершённый manifest.')
            if self._error is not None:
                raise ManifestError('Не удалось сохранить план или один из итогов PUT.') from self._error
            write_manifest(self.path, self._snapshot(complete=True, build_exit_code=build_exit_code))
            self._finished = True
