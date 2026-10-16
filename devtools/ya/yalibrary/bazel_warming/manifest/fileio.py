"""Потоковый gzip без распакованной копии на диске."""

from contextlib import contextmanager
import gzip
import zlib


class GzipError(OSError):
    """Повреждённый поток не позволяет безопасно прочитать или очистить файл."""


def is_gzip(source):
    """Определить сжатие по содержимому, сохранив позицию buffered reader."""
    return source.peek(2).startswith(b'\x1f\x8b')


@contextmanager
def decoded_stream(source):
    if is_gzip(source):
        try:
            with gzip.GzipFile(fileobj=source, mode='rb') as decoded:
                yield decoded
        except (gzip.BadGzipFile, EOFError, zlib.error) as error:
            raise GzipError('Повреждённый или неполный gzip-поток.') from error
    else:
        yield source


@contextmanager
def encoded_stream(destination, compressed):
    if compressed:
        # Имя временного файла и время записи не должны попадать в заголовок.
        with gzip.GzipFile(filename='', fileobj=destination, mode='wb', compresslevel=1, mtime=0) as encoded:
            yield encoded
    else:
        yield destination
