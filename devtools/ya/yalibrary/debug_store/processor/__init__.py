# encoding: utf-8

from __future__ import unicode_literals

import itertools

import base64
import json
import logging
import os
import shutil
import six
import typing as tp  # noqa: F401
from collections import defaultdict

from yalibrary.debug_store.processor._common import pretty_date
from yalibrary.debug_store.processor.html_generator import HTMLGenerator

try:
    from pathlib import Path
except ImportError:
    from pathlib2 import Path

from exts import yjson
import exts.fs
import exts.windows


class JsonEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, Path):
            return str(o)
        return repr(o)


class BaseDumpItem(object):
    logger = logging.getLogger(__name__ + ':BaseDumpItem')

    DUMP_FILE_NAME = "debug.json"
    FILES_LIST = "files.txt"
    HTML_FILE_NAME = "info.html"

    def __init__(self, workdir=None):
        # type: (tp.Optional[Path]) -> None

        self.workdir = None

        self.meta = dict(self._filter("__meta__", "argv"))

        if workdir:
            self.workdir = workdir  # type: Path

        self.debug_bundle_data = None  # type: dict[str, tp.Any]

    @property
    def with_info(self):
        return self.meta.get('__meta__', None) is not None

    def _lazy_read(self):
        # type: () -> tp.Iterable[tuple[str, int, str]]
        raise NotImplementedError()

    def _filter(self, *keys):
        for source, index, line in self._lazy_read():
            try:
                key, value = self._filter_line(line, keys)
                if key is None:
                    continue

                yield key, value
            except Exception as e:
                self.logger.warning("While parse %s:%s: %s", source, index, e)
                from traceback import format_exc

                self.logger.debug("%s", format_exc())

    def _filter_line(self, line, keys):
        # type: (tp.Union[str, dict], tp.Sequence[str]) -> tp.Tuple[tp.Optional[str], tp.Optional[tp.Any]]
        if isinstance(line, six.string_types):
            data = yjson.loads(six.ensure_str(line))
        else:
            data = line

        data  # type: dict

        key = data['key']
        value = data['value']
        value_type = data['value_type']

        if isinstance(value, str) and self._check_is_path(key, value, value_type):
            value = Path(value)

        if keys and key not in keys:
            return None, None

        return key, value

    @staticmethod
    def _check_executable(path):
        # type: (Path) -> bool
        if exts.windows.on_win():
            # os.access didn't work properly on Windows
            return path.name.endswith(".exe")
        else:
            return os.access(str(path), os.X_OK)

    @classmethod
    def _check_is_path(cls, key, value, value_type):
        # type: (str, tp.Any, str) -> bool

        is_path = False

        if key.endswith("_file") or key.endswith("_path"):
            is_path = True

        if value_type.endswith("Path"):
            is_path = True

        if not is_path:
            return False

        try:
            path = Path(value)  # type: Path
        except Exception:
            logging.error("While creating a path from `%s`", value)
            return False

        if path.exists() and not path.name.endswith("lock") and not cls._check_executable(path):
            return True

        return False

    def load(self):
        self.debug_bundle_data = dict(self._filter())

    def process(self, additional_paths=None, is_last=True):
        # type: (tp.Optional[list[tuple[str, Path]]], bool) -> Path
        assert self.workdir is not None
        additional_paths = additional_paths or []

        exts.fs.ensure_removed(str(self.workdir))
        self.workdir.mkdir(parents=True, exist_ok=True)

        if self.debug_bundle_data is None:
            self.load()

        debug_bundle_data = self.debug_bundle_data
        debug_bundle_file = self.workdir / self.DUMP_FILE_NAME
        self.logger.debug("Dump dict to %s", debug_bundle_file)

        with open(str(debug_bundle_file), "w") as f:
            json.dump(debug_bundle_data, f, sort_keys=True, indent=4, cls=JsonEncoder)

        processed_files = defaultdict(dict)

        paths = list(itertools.chain(self._find_paths(debug_bundle_data), additional_paths))
        for key, item in paths:
            rel_path_ = item.relative_to(item.parts[0])
            to_ = self.workdir / rel_path_
            to_.parent.mkdir(parents=True, exist_ok=True)
            _file_info = processed_files[str(rel_path_)]
            _file_info['original_path'] = str(item)
            _file_info['orig_key'] = key

            try:
                self.logger.debug("Copy from %s to %s", item, to_)
                if not item.exists():
                    self.logger.warning("Path does not exists: %s", item)
                    _file_info['type'] = "NONEXISTEN"
                    continue

                if item.is_file():
                    _file_info['type'] = "file"
                    shutil.copyfile(str(item), str(to_))
                elif item.is_dir():
                    _file_info['type'] = "dir"
                    exts.fs.copytree3(str(item), str(to_), dirs_exist_ok=True)
                else:
                    _file_info['type'] = "UNKNOWN"
                    raise NotImplementedError("Unknown file type: {}".format(item))

                _file_info['status'] = "OK"
            except Exception as e:
                self.logger.exception("While copy file from %s to %s", item, to_)
                _file_info['status'] = "ERROR"
                _file_info['exception'] = str(e)

        try:
            HTMLGenerator(
                debug_bundle_data, processed_files, debug_bundle_file.relative_to(self.workdir), is_last=is_last
            ).generate(self.workdir / self.HTML_FILE_NAME)
        except Exception:
            self.logger.exception("While creating HTML file")

        files_list = self.workdir / self.FILES_LIST
        self.logger.debug("Write file list info into %s", files_list)
        files_list.write_text(six.ensure_text(json.dumps(processed_files, sort_keys=True, indent=4)))

        return self.workdir

    @classmethod
    def _find_paths(cls, data):
        # type: (tp.Any) -> tp.Iterable[tuple[str, Path]]
        items_to_process = [("root", data)]  # type: list[tuple[str, Path]]
        while items_to_process:
            key, item = items_to_process.pop()

            if isinstance(item, Path):
                yield key, item
            elif isinstance(item, dict):
                items_to_process.extend(item.items())
            elif isinstance(item, list):
                items_to_process.extend((str(k), v) for k, v in enumerate(item))

    @property
    def description(self):
        return "Debug bundle from {}, v{}".format(self.init_time, self.version)

    @property
    def init_time(self):
        __meta__ = self.meta.get('__meta__', {})

        return pretty_date(__meta__.get('init_time', None))

    @property
    def version(self):
        return self.meta.get('__meta__', {}).get('version', "?")

    def __str__(self):
        argv = self.meta.get("argv", [None])

        cmd_line = "?"
        if argv[0] is not None:
            _ya_bin = argv[0]
            argv = [Path(_ya_bin).name] + argv[1:]

            cmd_line = " ".join(argv)

        return "`{}`: {} (v{})".format(
            cmd_line,
            self.init_time,
            self.version,
        )


class DumpItem(BaseDumpItem):
    logger = logging.getLogger(__name__ + ':DumpItem')

    DUMP_FILE_NAME = "debug.json"
    EVLOG_NAMESPACE = "dump_debug"

    def __init__(self, path, workdir=None):
        # type: (Path, tp.Optional[Path]) -> None

        self.path = Path(path)  # type: Path

        super(DumpItem, self).__init__(None if workdir is None else workdir / self.path.name)

    def _lazy_read(self):
        with self.path.open('rt') as f:
            index = 0

            lines = [None]
            while lines:
                lines = f.readlines(1)
                for line in lines:
                    index += 1
                    yield str(self.path), index, line

    def _filter_line(self, line, keys):
        # type: (tp.Union[str, dict], tp.Sequence[str]) -> tp.Tuple[tp.Optional[str], tp.Optional[tp.Any]]

        if isinstance(line, six.string_types):
            line = line
            ev = None
        elif isinstance(line, dict):
            line = str(line)
            ev = line
        else:
            raise NotImplementedError(type(line))

        if "dump_debug" not in line:
            return None, None

        if ev is None:
            try:
                ev = yjson.loads(line)
            except Exception as e:
                logging.warning("Skipped broken event: '%s' Content (base64): %s", e, base64.b64encode(line))
                return None, None

        if ev['namespace'] != self.EVLOG_NAMESPACE:
            return None, None

        if ev['event'] != "log":
            return None, None

        data = ev['value']

        return super(DumpItem, self)._filter_line(data, keys)


class BaseDumpProcessor(object):
    MISC_ROOT_SUB_PATH = "dump_debug"
    logger = logging.getLogger(__name__ + ':' + "BaseDumpProcessor")

    def __init__(self, path=None):
        self._path = Path(path) if path else path

        self._items_list = tuple(self._generate_items_list())  # type: tp.Tuple[BaseDumpItem]

    def _generate_items_list(self):
        # type: () -> tp.Iterable[BaseDumpItem]
        raise NotImplementedError()

    @property
    def path(self):
        # type: () -> Path

        if self._path is None:
            from core.config import misc_root

            self._path = Path(misc_root(), self.MISC_ROOT_SUB_PATH)

        self._path.mkdir(parents=True, exist_ok=True)

        return self._path

    def __iter__(self):
        return iter(self._items_list)

    def __getitem__(self, item):
        # type: (int) -> BaseDumpItem
        return self._items_list[-item]

    def __len__(self):
        return len(self._items_list)

    def __bool__(self):
        return bool(self._items_list)

    def __nonzero__(self):
        return bool(self._items_list)


class DumpProcessor(BaseDumpProcessor):
    logger = logging.getLogger(__name__ + ':' + "DumpProcessor")

    def __init__(self, path=None, evlog=None):
        self.evlog = evlog

        self._file_list = sorted(self.evlog)

        super(DumpProcessor, self).__init__(path)

    def _generate_items_list(self):
        for evlog_path in self._file_list:
            try:
                item = DumpItem(evlog_path, workdir=self.path)
                if item.with_info:
                    if self._check_dump_debug(item):
                        continue

                    yield item
            except Exception:
                self.logger.exception("While processing %s", evlog_path)

    def _check_dump_debug(self, item):
        # type: (DumpItem) -> bool
        argv = item.meta.get('argv', [])
        if len(argv) < 3:
            return False
        if argv[1:3] == ["dump", "debug"]:
            return True

        return False
