"""Чтение манифеста по узлам с помощью штатного JSONDecoder."""

import codecs
import json
import re

from .model import ManifestError, _unique_object

_READ_SIZE = 64 * 1024
_WHITESPACE = ' \t\r\n'
_SKIP_WHITESPACE = re.compile(r'[ \t\r\n]*').match
_DELIMITERS = _WHITESPACE + ',]}'


class _JSONStream:
    def __init__(self, source, expected_size, check):
        self.source = source
        self.expected_size = expected_size
        self.check = check
        self.bytes_read = 0
        self.buffer = ''
        self.offset = 0
        self.eof = False
        self.utf8 = codecs.getincrementaldecoder('utf-8')()
        self.decoder = json.JSONDecoder(object_pairs_hook=_unique_object)

    def _refill(self, amount=None):
        """Сохранить незавершённое значение и добавить ограниченные порции байтов."""
        if amount is None:
            amount = _READ_SIZE
        pieces = [self.buffer[self.offset :]]
        self.offset = 0
        while amount > 0 and not self.eof:
            if self.check is not None:
                self.check()
            size = min(_READ_SIZE, amount)
            if self.expected_size is not None:
                size = min(size, self.expected_size + 1 - self.bytes_read)
            chunk = self.source.read(size)
            self.bytes_read += len(chunk)
            if self.expected_size is not None and (
                self.bytes_read > self.expected_size or (not chunk and self.bytes_read != self.expected_size)
            ):
                raise ManifestError('Размер манифеста изменился во время чтения.')
            self.eof = not chunk
            try:
                pieces.append(self.utf8.decode(chunk, final=self.eof))
            except UnicodeDecodeError as error:
                raise ManifestError(f'Не удалось прочитать JSON-манифест: {error}.') from error
            amount -= len(chunk)
        self.buffer = ''.join(pieces)

    def peek(self):
        """Пропустить только разрешённые JSON-пробелы и вернуть следующий символ."""
        while True:
            self.offset = _SKIP_WHITESPACE(self.buffer, self.offset).end()
            if self.offset < len(self.buffer):
                return self.buffer[self.offset]
            if self.eof:
                return ''
            self._refill()

    def fail(self, message):
        raise ManifestError(f'Не удалось прочитать JSON-манифест: {message}.')

    def take(self, character):
        if self.peek() != character:
            self.fail(f'ожидается {character!r}')
        self.offset += 1

    def value(self):
        self.peek()
        while True:
            try:
                result, end = self.decoder.raw_decode(self.buffer, self.offset)
            except json.JSONDecodeError as error:
                if self.eof:
                    raise ManifestError(f'Не удалось прочитать JSON-манифест: {error}.') from error
            except ManifestError:
                raise
            except (ValueError, RecursionError) as error:
                raise ManifestError(f'Не удалось прочитать JSON-манифест: {error}.') from error
            else:
                # raw_decode может вернуть 1 из незавершённых 123 или 1e3.
                if (
                    type(result) not in (int, float)
                    or self.eof
                    or (end < len(self.buffer) and self.buffer[end] in _DELIMITERS)
                ):
                    self.offset = end
                    return result
            # Повторный разбор большого узла выполняется при геометрическом росте
            # буфера. Для повреждённого значения может понадобиться весь остаток файла.
            self._refill(max(_READ_SIZE, len(self.buffer) - self.offset))


def read_document(source, expected_size, node_from_dict, fields, check=None):
    """Сразу преобразовывать узлы, сохраняя произвольный порядок полей заголовка."""
    stream = _JSONStream(source, expected_size, check)
    stream.take('{')
    data = {}
    if stream.peek() != '}':
        while True:
            if stream.peek() != '"':
                stream.fail('имя поля должно быть JSON-строкой')
            key = stream.value()
            if key in data:
                raise ManifestError(f'Повторный JSON-ключ: {key!r}.')
            if key not in fields:
                raise ManifestError(f'Неизвестное поле манифеста: {key!r}.')
            stream.take(':')
            if key == 'nodes':
                stream.take('[')
                nodes = []
                if stream.peek() != ']':
                    while True:
                        if stream.peek() != '{':
                            stream.fail('узел должен быть JSON-объектом')
                        nodes.append(node_from_dict(stream.value()))
                        if stream.peek() == ']':
                            break
                        stream.take(',')
                stream.take(']')
                data[key] = nodes
            else:
                data[key] = stream.value()
            if stream.peek() == '}':
                break
            stream.take(',')
    stream.take('}')
    if stream.peek():
        stream.fail('после документа обнаружены лишние данные')
    return data
