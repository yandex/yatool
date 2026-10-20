"""Строгий формат manifest: проверка схемы, идентификатора запуска и файловых путей."""

from dataclasses import dataclass
import os
from pathlib import Path, PurePosixPath
import re
import stat

# -----------------------------------------------------------------------------
# Формат документа и проверки простых значений
# -----------------------------------------------------------------------------

SCHEMA_VERSION = 1
_MANIFEST_FIELDS = ('schema_version', 'run_id', 'complete', 'build_exit_code', 'max_file_size', 'nodes')
# Ограничиваем число UID, чтобы сбой кеша не запускал массовую пересборку.
# У одного UID может быть несколько файлов CAS; лимит относится к результатам сборки.
MAX_CACHE_REPAIR_RESULTS = 100
PUT_STATUSES = frozenset(('not_attempted', 'ok', 'failed', 'skipped_size'))
_RUN_ID = re.compile(r'[A-Za-z0-9_.-]{1,128}\Z')


class ManifestError(ValueError):
    """Manifest не позволяет достоверно определить результат текущего запуска."""


def validate_run_id(value):
    """Разрешить короткий идентификатор без пробелов, управляющих символов и путей."""
    if not isinstance(value, str) or _RUN_ID.fullmatch(value) is None:
        raise ManifestError('run_id должен содержать от 1 до 128 латинских букв, цифр или символов _.-.')


def _validate_uid(value):
    if (
        not isinstance(value, str)
        or not value
        or any(ord(character) < 32 or ord(character) == 127 for character in value)
    ):
        raise ManifestError('uid должен быть непустой строкой без управляющих символов.')


def _integer(value, name, minimum=0):
    """Отклонить bool, дробные и отрицательные значения в числовых полях."""
    if type(value) is not int or value < minimum:
        raise ManifestError(f'{name} должен быть целым числом не меньше {minimum}.')


def relative_output(value):
    """Превратить выход графа $(BUILD_ROOT)/... в безопасный относительный путь."""
    if not isinstance(value, str):
        raise ManifestError('Путь выхода должен быть строкой.')
    prefix = '$(BUILD_ROOT)/'
    if value.startswith(prefix):
        value = value[len(prefix) :]
    if (
        not value
        or '\\' in value
        or any(
            ord(character) < 32 or ord(character) == 127 or 0xD800 <= ord(character) <= 0xDFFF for character in value
        )
        or PurePosixPath(value).is_absolute()
        or any(part in ('.', '..', '') for part in value.split('/'))
        or any(len(part.encode('utf-8')) > 255 for part in value.split('/'))
        or '$(' in value
    ):
        raise ManifestError(f'Некорректный относительный путь выхода: {value!r}.')
    return value


def _paths(paths):
    """Проверить уникальность путей и конфликт файла с его собственным потомком."""
    if not isinstance(paths, (tuple, list)):
        raise ManifestError('outputs должен быть списком путей.')
    seen = set()
    for path in paths:
        if relative_output(path) != path or path in seen:
            raise ManifestError(f'Некорректный или повторный путь: {path!r}.')
        seen.add(path)
    for path in seen:
        parts = path.split('/')
        if any('/'.join(parts[:index]) in seen for index in range(1, len(parts))):
            raise ManifestError(f'Путь файла конфликтует с его потомком: {path!r}.')


def _object(data, expected, name):
    """Не принимать пропущенные или неизвестные поля текущей версии формата."""
    if not isinstance(data, dict) or set(data) != set(expected):
        raise ManifestError(f'Неверный набор полей {name}; ожидаются: {", ".join(sorted(expected))}.')


# -----------------------------------------------------------------------------
# Независимые модели плана и выполненных записей
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class WrittenFile:
    path: str
    size: int
    symlink: str | None = None

    def validate(self):
        if relative_output(self.path) != self.path:
            raise ManifestError('written_files должен содержать относительные пути.')
        _integer(self.size, 'written_files.size')
        if self.symlink is not None and (
            not isinstance(self.symlink, str) or not self.symlink or '\0' in self.symlink or os.path.isabs(self.symlink)
        ):
            raise ManifestError('Цель симлинка должна быть непустым относительным путём.')

    @classmethod
    def from_dict(cls, data):
        expected = ('path', 'size', 'symlink') if isinstance(data, dict) and 'symlink' in data else ('path', 'size')
        _object(data, expected, 'written_files')
        result = cls(**data)
        result.validate()
        return result


@dataclass(frozen=True)
class Node:
    uid: str
    outputs: tuple[str, ...]
    cacheable: bool = True
    put_status: str = 'not_attempted'
    written_files: tuple[WrittenFile, ...] = ()
    build_error: str = ''

    def validate(self, max_file_size):
        """Подтвердить пропуск по размеру реальным большим выходом данного узла."""
        _validate_uid(self.uid)
        _paths(self.outputs)
        if not self.outputs:
            raise ManifestError(f'У узла {self.uid!r} не указаны выходы.')
        if (
            type(self.cacheable) is not bool
            or not isinstance(self.put_status, str)
            or self.put_status not in PUT_STATUSES
        ):
            raise ManifestError('Некорректные cacheable или put_status.')
        if not isinstance(self.build_error, str):
            raise ManifestError('build_error должен быть строкой.')
        if self.build_error and self.put_status != 'not_attempted':
            raise ManifestError('Несобранный узел не должен содержать итог PUT.')
        if not isinstance(self.written_files, (tuple, list)):
            raise ManifestError('written_files должен быть списком.')
        for item in self.written_files:
            if not isinstance(item, WrittenFile):
                raise ManifestError('written_files должен содержать WrittenFile.')
            item.validate()
        paths = [item.path for item in self.written_files]
        _paths(paths)
        if self.put_status == 'not_attempted':
            if self.written_files:
                raise ManifestError('Для not_attempted не должно быть written_files.')
        elif not self.cacheable or not set(self.outputs).issubset(paths):
            raise ManifestError('Итог PUT должен содержать все выходы cacheable-узла.')
        if self.put_status == 'skipped_size':
            if not max_file_size or not any(item.size > max_file_size for item in self.written_files):
                raise ManifestError('skipped_size не подтверждён выходом больше max_file_size.')

    @classmethod
    def from_dict(cls, data, max_file_size):
        result = cls._from_dict(data)
        result.validate(max_file_size)
        return result

    @classmethod
    def _from_dict(cls, data):
        """Преобразовать узел до проверки ограничений из заголовка манифеста."""
        expected = ('uid', 'outputs', 'cacheable', 'put_status', 'written_files')
        if isinstance(data, dict) and 'build_error' in data:
            expected += ('build_error',)
        _object(data, expected, 'nodes')
        if not isinstance(data['outputs'], list) or not isinstance(data['written_files'], list):
            raise ManifestError('outputs и written_files должны быть JSON-массивами.')
        return cls(
            uid=data['uid'],
            outputs=tuple(data['outputs']),
            cacheable=data['cacheable'],
            put_status=data['put_status'],
            written_files=tuple(WrittenFile.from_dict(item) for item in data['written_files']),
            build_error=data.get('build_error', ''),
        )

    @property
    def requires_verification(self):
        """Даже успешный PUT требует чтения AC/CAS; исключаются только доказанные пропуски."""
        return self.cacheable and self.put_status != 'skipped_size'


@dataclass(frozen=True)
class LocalCacheDescriptor:
    kind: str
    path: str

    def validate(self):
        if self.kind not in ('new_store', 'ring_store'):
            raise ManifestError('Неизвестный тип локального кеша прогрева.')
        if not isinstance(self.path, str) or '\0' in self.path or not Path(self.path).is_absolute():
            raise ManifestError('Путь локального кеша должен быть абсолютным.')

    def to_dict(self):
        self.validate()
        return {'kind': self.kind, 'path': self.path}

    @classmethod
    def from_dict(cls, data):
        _object(data, ('kind', 'path'), 'local_cache')
        result = cls(**data)
        result.validate()
        return result


@dataclass(frozen=True)
class Manifest:
    run_id: str
    nodes: tuple[Node, ...]
    max_file_size: int = 0
    complete: bool = False
    build_exit_code: int | None = None
    schema_version: int = SCHEMA_VERSION
    local_cache: LocalCacheDescriptor | None = None

    def validate(self):
        """Проверить весь документ, включая связь complete с завершением сборки."""
        validate_run_id(self.run_id)
        if type(self.schema_version) is not int or self.schema_version != SCHEMA_VERSION:
            raise ManifestError(f'Поддерживается только schema_version={SCHEMA_VERSION}.')
        _integer(self.max_file_size, 'max_file_size')
        if type(self.complete) is not bool:
            raise ManifestError('complete должен быть true или false.')
        if self.complete:
            _integer(self.build_exit_code, 'build_exit_code')
        elif self.build_exit_code is not None:
            raise ManifestError('Незавершённый manifest не должен содержать код завершения сборки.')
        if self.local_cache is not None:
            if not isinstance(self.local_cache, LocalCacheDescriptor):
                raise ManifestError('Некорректное описание локального кеша.')
            self.local_cache.validate()
        if not isinstance(self.nodes, (tuple, list)):
            raise ManifestError('nodes должен быть списком.')
        seen = set()
        for node in self.nodes:
            if not isinstance(node, Node):
                raise ManifestError('nodes должен содержать Node.')
            node.validate(self.max_file_size)
            if node.uid in seen:
                raise ManifestError(f'Повторный UID в manifest: {node.uid!r}.')
            seen.add(node.uid)

    def to_dict(self):
        """Вернуть JSON-объект: коллекции в нём представлены массивами, а не tuple."""
        self.validate()
        return {
            'schema_version': self.schema_version,
            'run_id': self.run_id,
            'complete': self.complete,
            'build_exit_code': self.build_exit_code,
            'max_file_size': self.max_file_size,
            'nodes': [
                {
                    'uid': node.uid,
                    'outputs': list(node.outputs),
                    'cacheable': node.cacheable,
                    'put_status': node.put_status,
                    'written_files': [
                        {
                            'path': item.path,
                            'size': item.size,
                            **({'symlink': item.symlink} if item.symlink is not None else {}),
                        }
                        for item in node.written_files
                    ],
                    **({'build_error': node.build_error} if node.build_error else {}),
                }
                for node in self.nodes
            ],
            **({'local_cache': self.local_cache.to_dict()} if self.local_cache is not None else {}),
        }

    @classmethod
    def from_dict(cls, data):
        return cls._from_dict(data, Node._from_dict)

    @classmethod
    def _from_dict(cls, data, node_from_dict):
        """Собрать манифест из JSON или уже прочитанных узлов."""
        expected = _MANIFEST_FIELDS
        if isinstance(data, dict) and 'local_cache' in data:
            expected += ('local_cache',)
        _object(data, expected, 'manifest')
        if type(data['schema_version']) is not int or data['schema_version'] != SCHEMA_VERSION:
            raise ManifestError(f'Поддерживается только schema_version={SCHEMA_VERSION}.')
        _integer(data['max_file_size'], 'max_file_size')
        if not isinstance(data['nodes'], list):
            raise ManifestError('nodes должен быть JSON-массивом.')
        result = cls(
            run_id=data['run_id'],
            nodes=tuple(node_from_dict(node) for node in data['nodes']),
            max_file_size=data['max_file_size'],
            complete=data['complete'],
            build_exit_code=data['build_exit_code'],
            schema_version=data['schema_version'],
            local_cache=LocalCacheDescriptor.from_dict(data['local_cache']) if 'local_cache' in data else None,
        )
        result.validate()
        return result


# -----------------------------------------------------------------------------
# Чтение и проверка manifest
# -----------------------------------------------------------------------------


def _unique_object(pairs):
    """JSON с повторным ключом не имеет однозначной интерпретации."""
    result = {}
    for key, value in pairs:
        if key in result:
            raise ManifestError(f'Повторный JSON-ключ: {key!r}.')
        result[key] = value
    return result


def _load_manifest(path, check=None):
    """Прочитать и проверить документ, включая незавершённый."""
    from .stream import read_document
    from .fileio import GzipError, decoded_stream, is_gzip

    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW
    descriptor = os.open(Path(path), flags)
    with os.fdopen(descriptor, 'rb') as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ManifestError('Manifest должен быть обычным файлом.')
        compressed = is_gzip(source)
        try:
            with decoded_stream(source) as decoded:
                data = read_document(
                    decoded,
                    expected_size=None if compressed else info.st_size,
                    node_from_dict=Node._from_dict,
                    fields=(*_MANIFEST_FIELDS, 'local_cache'),
                    check=check,
                )
        except GzipError as error:
            raise ManifestError('Не удалось прочитать gzip-манифест: повреждённый или неполный поток.') from error
        if os.fstat(source.fileno()).st_size != info.st_size or source.tell() != info.st_size:
            raise ManifestError('Размер манифеста изменился во время чтения.')
    return Manifest._from_dict(data, lambda node: node)


def read_manifest(path, expected_run_id, *, require_complete=True):
    """Проверить план текущего запуска; незавершённый допустим только для диагностики."""
    validate_run_id(expected_run_id)
    result = _load_manifest(path)
    if result.run_id != expected_run_id:
        raise ManifestError('Manifest принадлежит другому запуску.')
    if require_complete and not result.complete:
        raise ManifestError('Сборка не завершила manifest; полнота результатов неизвестна.')
    return result
