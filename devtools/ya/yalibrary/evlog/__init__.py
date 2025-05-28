import contextlib
import datetime
import io
import logging
import os
import sys
import threading
import time

import six
import zstandard as zstd

import devtools.ya.core.gsid
from exts import fs, os2, yjson, archive

import typing as tp  # noqa
import abc


_LOG_FILE_NAME_FMT = '%H-%M-%S'
_LOG_DIR_NAME_FMT = '%Y-%m-%d'
DAYS_TO_SAVE = 10


class EvlogSuffix:
    ZST = '.evlog.zst'
    ZSTD = '.evlog.zstd'
    JSON = '.evlog'

    @classmethod
    def all(cls):
        return cls.ZST, cls.ZSTD, cls.JSON


def _fix_non_utf8(data):
    from six.moves import collections_abc

    def _decode_non_ut8_item(data, errors='replace'):
        return data.decode('UTF-8', errors=errors) if isinstance(data, six.binary_type) else data

    res = {}
    for k, v in data.items():
        if isinstance(v, six.binary_type):
            v = _decode_non_ut8_item(v)
        elif isinstance(v, (dict, type(os.environ))):
            v = _fix_non_utf8(v)
        elif isinstance(v, collections_abc.Iterable):
            v = list(map(_decode_non_ut8_item, v))
        res[_decode_non_ut8_item(k)] = v
    return res


def is_compressed(filepath):
    return archive.get_filter_type(filepath) == "zstd"


class EvlogReader:
    def __init__(self, filepath):
        self.filepath = filepath

    @contextlib.contextmanager
    def _get_stream(self):
        if is_compressed(self.filepath):
            dctx = zstd.ZstdDecompressor()

            with open(self.filepath, 'rb') as afile:
                stream_reader = dctx.stream_reader(afile)
                text_stream = io.TextIOWrapper(stream_reader, encoding='utf-8')
                yield text_stream
        else:
            with open(self.filepath, 'rt') as afile:
                yield afile

    def __iter__(self):
        with self._get_stream() as stream:
            for nline, line in enumerate(stream):
                try:
                    entry = yjson.loads(line)
                    if not isinstance(entry, dict):
                        logging.warning("Evlog entry is not a dict, skip %d line. File: %s", nline + 1, self.filepath)
                        continue
                    yield entry
                except Exception:
                    logging.warning("Skip broken entry at %d line. File: %s", nline + 1, self.filepath)
                    continue


class EmptyEvlogListException(Exception):
    mute = True


class BaseEvlogWriter:
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def __init__(self, filepath, replacements=None):
        # type: (str, dict | None) -> None
        pass

    @abc.abstractmethod
    def write(self, namespace, event, **kwargs):
        # type: (str, str) -> None
        pass

    def get_writer(self, namespace):
        # type: (str) -> tp.Callable[[str], None]

        def inner(event, **kwargs):
            self.write(namespace, event, **kwargs)

        return inner


class DummyEvlogWriter(BaseEvlogWriter):
    def __init__(self, filepath=None, replacements=None):
        pass

    def write(self, namespace, event, **kwargs):
        pass

    def __call__(self, *args, **kwargs):
        pass


class EvlogWriter(BaseEvlogWriter):
    def __init__(self, filepath, replacements=None):
        self.filepath = filepath
        self._fileobj = self._open_file(filepath)
        self._lock = threading.Lock()
        self._replacements = self._get_replacements(replacements)

    @staticmethod
    def _need_to_be_compressed(filepath):
        # TODO: fix me in YA-1998
        return filepath.endswith(('.zstd', '.zst'))

    @classmethod
    def _open_file(cls, filepath):
        if cls._need_to_be_compressed(filepath):
            if six.PY3:
                return zstd.open(filepath, 'w', cctx=zstd.ZstdCompressor(level=1))
            # XXX Remove when YA-261 is done
            else:
                return zstd.ZstdCompressor(level=1).stream_writer(open(filepath, 'w'))

        return open(filepath, 'w')

    @classmethod
    def _get_replacements(cls, replacements):
        new_replacements = []

        if not replacements:
            for k, v in six.iteritems(os.environ):
                if k.endswith('TOKEN') and cls.__json_safe(v) and v:
                    new_replacements.append(v)
        else:
            for v in replacements:
                if cls.__json_safe(v) and v:
                    new_replacements.append(v)

        new_replacements.sort()
        return new_replacements

    @staticmethod
    def __json_safe(s):
        falsy = frozenset(['"', "[", "]", "{", "}", "\\", ",", ":"])

        if any([char in s for char in falsy]):
            return False

        return s not in frozenset(["true", "false", "null"])

    def _remove_secrets(self, s):
        for r in self._replacements:
            s = s.replace(r, "[SECRET]")
        return s

    def write(self, namespace, event, **kwargs):
        value = {
            'timestamp': time.time(),
            'thread_name': threading.current_thread().name,
            'namespace': namespace,
            'event': event,
            'value': kwargs,
        }
        try:
            s = yjson.dumps(value) + '\n'
        except (UnicodeDecodeError, OverflowError):
            s = yjson.dumps(_fix_non_utf8(value)) + '\n'

        s = self._remove_secrets(s)

        with self._lock:
            try:
                self._fileobj.write(s)
            except IOError as e:
                import errno

                if e.errno == errno.ENOSPC:
                    sys.stderr.write("No space left on device, clean up some files. Try running 'ya gc cache'\n")
                    sys.stderr.write(s)
                else:
                    raise e

    def close(self):
        self._fileobj.close()

    @property
    def closed(self):
        return self._fileobj.closed


class EvlogFileFinder(object):
    def __init__(self, evlog_dir, filter_func=lambda f: True):
        self._evlog_dir = evlog_dir
        self._filter_func = filter_func

    def __iter__(self):
        evlog_suffixes = EvlogSuffix.all()
        candidates = []
        for root, dirs, files in os2.fastwalk(self._evlog_dir):
            candidates.extend(os.path.join(root, f) for f in files if f.endswith(evlog_suffixes))

        # sorting matters when searching for the latest one
        for f in sorted(candidates, reverse=True):
            if self._filter_func(f):
                yield f

    def get_latest(self):
        for evlog in self:
            return evlog
        raise EmptyEvlogListException('Empty event logs list')


class BaseEvlogFacade:
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def __init__(self, evlog_dir, filename=None, replacements=None, compress_evlog=True):
        pass

    @abc.abstractmethod
    def write(self, namespace, event, **kwargs):
        # type: (str, str) -> None
        pass

    @abc.abstractmethod
    def get_writer(self, namespace):
        # type: (str) -> tp.Callable[[str], None]
        pass


class DummyEvlogFacade(BaseEvlogFacade):
    def __init__(self, evlog_dir=None, filename=None, replacements=None, compress_evlog=True):
        self.writer = DummyEvlogWriter()

    def write(self, namespace, event, **kwargs):
        return self.writer.write(namespace, event, **kwargs)

    def get_writer(self, namespace):
        return self.writer.get_writer(namespace)


class EvlogFacade(BaseEvlogFacade):
    def __init__(self, evlog_dir, filename=None, replacements=None, compress_evlog=True):
        fs.create_dirs(evlog_dir)

        self._evlog_dir = evlog_dir
        self._now_time = datetime.datetime.now()

        filepath = filename or self._gen_default_filepath(compress_evlog)
        logging.debug('Event log file is %s', filepath)

        self.writer = EvlogWriter(filepath, replacements)
        self.file_finder = EvlogFileFinder(evlog_dir, lambda f: os.path.basename(f) != os.path.basename(filepath))

    @property
    def filepath(self):
        return self.writer.filepath

    def write(self, namespace, event, **kwargs):
        return self.writer.write(namespace, event, **kwargs)

    def get_writer(self, namespace):
        return self.writer.get_writer(namespace)

    def close(self):
        self.writer.close()

    @property
    def closed(self):
        return self.writer.closed

    def __iter__(self):
        for f in self.file_finder:
            yield f

    def get_latest(self):
        return self.file_finder.get_latest()

    def _gen_default_filepath(self, compress_evlog):
        run_uid = devtools.ya.core.gsid.uid()
        sfx = EvlogSuffix.ZST if compress_evlog else EvlogSuffix.JSON
        default_filename = self._now_time.strftime(_LOG_FILE_NAME_FMT) + '.' + run_uid + sfx
        chunk_name = self._now_time.strftime(_LOG_DIR_NAME_FMT)
        fs.create_dirs(os.path.join(self._evlog_dir, chunk_name))
        return os.path.join(self._evlog_dir, chunk_name, default_filename)

    def cleanup_old_dirs(self):
        def parse_log_dir(x):
            return datetime.datetime.strptime(x, _LOG_DIR_NAME_FMT)

        def older_than(x):
            return self._now_time - x > datetime.timedelta(days=DAYS_TO_SAVE)

        for _dir in os.listdir(self._evlog_dir):
            try:
                if older_than(parse_log_dir(_dir)):
                    fs.remove_tree_safe(os.path.join(self._evlog_dir, _dir))
            except Exception:
                logging.debug("While analysing %s:", _dir, exc_info=sys.exc_info())


def with_evlog(params, evlog_dir, hide_token):
    filename = getattr(params, 'evlog_file', None)
    compress_evlog = getattr(params, 'compress_evlog', True)
    evlog = EvlogFacade(evlog_dir, filename, hide_token, compress_evlog)
    evlog.cleanup_old_dirs()
    evlog.write('init', 'init', args=sys.argv, env=os.environ.copy())

    try:
        yield evlog
    finally:
        evlog.close()
