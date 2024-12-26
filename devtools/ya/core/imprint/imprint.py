import os
import logging

import six

from exts import func, os2, hashing
from devtools.ya.core.config import misc_root, find_root

# from yalibrary.monitoring import YaMonEvent

from .base import SimpleMapper, BaseCache, BaseFileCache
from .change_list import ChangeList


class ArcPath:
    strong_mode = False
    logger = logging.getLogger(__name__ + ":ArcPath")

    @classmethod
    @func.lazy
    def _arcadia_root(cls):
        return os.path.join(find_root(), "")

    @classmethod
    def to_rel(cls, path):
        path = os.path.normpath(path)
        if os.path.isabs(path):
            abs_path = path
        else:
            abs_path = os.path.abspath(path)

        if abs_path.startswith(cls._arcadia_root()):
            rel_arc_path = os.path.relpath(abs_path, start=cls._arcadia_root())
            return rel_arc_path.strip(os.sep).replace(os.sep, '/')

        msg = "Path not from arcadia: {}, should startswith: ({})".format(abs_path, cls._arcadia_root())

        cls.logger.warning(msg)
        if cls.strong_mode:
            raise ValueError(msg)
        return abs_path

    @classmethod
    def to_abs(cls, rel_path):
        rel_path = os.path.normpath(rel_path)

        if os.path.isabs(rel_path):
            return rel_path

        path = os.path.join(cls._arcadia_root(), rel_path)

        return path


class YaStoredCache(BaseFileCache):
    CACHE_PATH_DEFAULT = "{misc_root}/conf/cache/"
    CACHE_FILE_DEFAULT = "fs.cache.{version}"
    CACHE_VERSION = 1

    def __init__(self, name, f, read=True, write=True, cache_source_path=None, process_arcadia_clash=True):
        self.cache_source_path = cache_source_path
        self.process_arcadia_clash = process_arcadia_clash

        super(YaStoredCache, self).__init__(name, f, check=self._mtime_files_check, read=read, write=write)

    def _generate_cache_path_parts(self):
        if self.cache_source_path:
            folder_format = self.cache_source_path
        else:
            folder_format = self.CACHE_PATH_DEFAULT

        file_format = self.CACHE_FILE_DEFAULT

        if self.process_arcadia_clash:
            file_format += "-{arcadia_hash}"

        fmt_keys = dict(misc_root=misc_root(), arcadia_hash=hashing.fast_hash(find_root()), version=self.CACHE_VERSION)

        path_name = folder_format.format(**fmt_keys)
        file_name = file_format.format(**fmt_keys)

        return path_name, file_name

    def _generate_source_path(self):
        path_name, file_name = self._generate_cache_path_parts()

        self._create_dirs(path_name)

        return os.path.join(path_name, file_name)

    @staticmethod
    def _mtime_files_check(abs_path, result):
        return os.stat(abs_path).st_mtime != result['check']

    def _update_cache(self, abs_path, result):
        return super(YaStoredCache, self)._update_cache(
            abs_path, {'value': result, 'check': os.stat(abs_path).st_mtime}
        )

    def _do_calcs(self, abs_paths):
        for abs_path, result in super(YaStoredCache, self)._do_calcs(abs_paths):
            yield abs_path, result['value']

    def __getitem__(self, item):
        return super(YaStoredCache, self).__getitem__(item)['value']

    def _load_processor(self, data):
        for rel_path, result in six.iteritems(data):
            arc_path = ArcPath.to_abs(rel_path)
            if arc_path:
                yield arc_path, result

    def _store_processor(self, data):
        for abs_path, result in six.iteritems(data):
            yield ArcPath.to_rel(abs_path), result

    def _with_parents(self):
        root = find_root()

        results = set()

        for path in self._cache.keys():
            parent = path
            while parent and (parent != "/") and (parent != root) and (parent not in results):
                results.add(parent)
                parent = os.path.dirname(parent)

        return results

    def _invalidate(self, rel_paths):
        self.logger.debug("Start invalidation")
        abs_paths = map(ArcPath.to_abs, rel_paths)

        self.logger.debug("Create sorted list of files (%s)", len(self._cache.keys()))
        sorted_items = tuple(sorted(self._with_parents()))

        self.logger.debug("Create index map for files")
        items_indexes = {item: index for index, item in enumerate(sorted_items)}

        self.logger.debug("Calculate paths for invalidate")
        to_invalidate = set()

        for abs_path in abs_paths:
            # clean child files
            index = items_indexes.get(abs_path, None)
            if index:
                to_invalidate.add(abs_path)
                index += 1

                abs_folder_path = os.path.join(abs_path, "")  # Add separator

                while index < len(sorted_items) and sorted_items[index].startswith(abs_folder_path):
                    to_invalidate.add(sorted_items[index])
                    index += 1

        # Remove items
        for item in to_invalidate:
            if item in self._cache:
                del self._cache[item]

        # Clean sub-cache
        # TODO: Duplicated part of code from super()
        if isinstance(self.f, BaseCache):
            self.f._invalidate(rel_paths)

        self.logger.debug("Invalidated %d items", len(to_invalidate))


class ImprintException(Exception):
    pass


class Imprint:
    def __init__(self, excluded=None):
        self.logger = logging.getLogger(__name__ + ":" + self.__class__.__name__)

        self._rel_path = BaseCache("to_rel", SimpleMapper(ArcPath.to_rel))
        self._content_hash = self._new_content_hash()
        self._iter_files = BaseCache("iter_cache", SimpleMapper(lambda item: sorted(self._do_iter_files(item))))
        self._dir_cache = BaseCache("dir_cache", lambda *items: dict(self._do(items)))

        self._excluded_dirs = (excluded or tuple()) + ('.svn', '.cache', '.idea')

        self.strong_mode = False

        self._change_list_applied = False

    @staticmethod
    def _new_content_hash():
        return BaseCache("content_hash", SimpleMapper(hashing.fast_filehash))

    def enable_fs(self, read=True, write=True, cache_source_path=None, process_arcadia_clash=True, quiet=False):
        # TODO: Check -xx
        if isinstance(self._content_hash, YaStoredCache):
            self.logger.warning("FS MD5 cache already enabled")
            if quiet:
                return
            raise ValueError("FS MD5 cache already enabled")

        self.logger.debug("Enabling MD5 FS cache (%s)", cache_source_path)

        self._content_hash.clear()

        self._content_hash = YaStoredCache(
            'content_hash_fs',
            self._content_hash,
            read=read,
            write=write,
            cache_source_path=cache_source_path,
            process_arcadia_clash=process_arcadia_clash,
        )

    def disable_fs(self):
        self.logger.debug("Disabling MD5 FS cache")
        if isinstance(self._content_hash, YaStoredCache):
            del self._content_hash
            self._content_hash = self._new_content_hash()

    def __call__(self, *abs_paths):
        self._check_args(abs_paths)
        return self._dir_cache(*abs_paths)

    def _do(self, abs_paths):
        all_files = sum(six.itervalues(self._iter_files(*abs_paths)), [])

        self._rel_path.warm_up(*all_files)
        self._rel_path.warm_up(*abs_paths)
        self._content_hash.warm_up(*all_files)

        for abs_path in abs_paths:
            if self._is_build_file(abs_path):
                yield abs_path, self._do_file_bc(abs_path)
            elif self._is_build_dir(abs_path):
                yield abs_path, self._do_dir(abs_path)
            else:
                self.logger.warning("Path is not buildable target: %s", abs_path)

                if self.strong_mode:
                    raise ImprintException("Path is not buildable target: ", abs_path, dict(self._get_causes(abs_path)))
                else:
                    yield abs_path, self._do_not_buildable_target(abs_path)

        self._iter_files.clear()
        # Not need _iter_files cache anymore

    def _do_file_bc(self, abs_path):
        # FIXME: Backward compatability !!
        return self.combine_imprints(self._rel_path[abs_path], self._do_file(abs_path))

    def _do_file(self, abs_path):
        return self.combine_imprints(self._rel_path[abs_path], self._content_hash[abs_path])

    def _do_dir(self, abs_path):
        files = self._iter_files[abs_path]

        return self.combine_imprints(self._rel_path[abs_path], *(self._do_file(file_path) for file_path in files))

    def _do_not_buildable_target(self, abs_path):
        return self.combine_imprints(self._rel_path[abs_path])

    def _get_causes(self, abs_path):
        if not os.path.isfile(abs_path) and not os.path.isdir(abs_path):
            yield "file", False
            yield "dir", False

        if os.path.islink(abs_path):
            yield "link", True

        if abs_path.endswith('.pyc'):
            yield ".pyc", True

        if os.path.basename(abs_path) in self._excluded_dirs:
            yield "excluded", True

    @staticmethod
    @func.memoize()
    def _is_build_file(abs_p):
        return os.path.isfile(abs_p) and not os.path.islink(abs_p) and not abs_p.endswith('.pyc')

    @func.memoize()
    def _is_build_dir(self, abs_p):
        return os.path.isdir(abs_p) and not os.path.islink(abs_p) and not os.path.basename(abs_p) in self._excluded_dirs

    def _do_iter_files(self, abs_path, yield_dirs=False, do_recursively=True):
        if self._is_build_file(abs_path):
            yield abs_path

        elif os.path.isdir(abs_path):
            if yield_dirs:
                yield abs_path

            for root, dirs, files in os2.fastwalk(abs_path):
                for d in [p for p in dirs if not self._is_build_dir(os.path.join(root, p))]:
                    dirs.remove(d)

                if yield_dirs:
                    for d in dirs:
                        yield os.path.join(root, d)

                for file_name in files:
                    abs_file = os.path.join(root, file_name)

                    if self._is_build_file(abs_file):
                        yield abs_file

                if not do_recursively:
                    return
        else:
            self.logger.warning("%s should be either file or folder to generate imprint for", abs_path)

    @staticmethod
    def _check_args(args):
        if len(args) == 1:
            item = args[0]
            if isinstance(item, list) or isinstance(item, tuple) or isinstance(item, set):
                raise TypeError("Use * for arguments! Bad type", type(item), item)

    @staticmethod
    def combine_imprints(*imprints):
        Imprint._check_args(imprints)
        return str(hashing.fast_hash('\x01'.join(map(str, imprints))))

    def clear(self):
        self._rel_path.clear()
        self._content_hash.clear()
        self._iter_files.clear()
        self._dir_cache.clear()

    def store(self):
        self._content_hash.store()

    def load(self):
        self._content_hash.clear()
        self._content_hash.load()

    def stats(self):
        self.logger.debug(self._rel_path.stats)
        self.logger.debug(self._content_hash.stats)
        # Iter files just helper cache
        # self.logger.debug(self._iter_files.stats)
        self.logger.debug(self._dir_cache.stats)

        # for name, stats in {
        #     'ImprintRelPath': self._rel_path.stats,
        #     'ImprintContentHash': self._content_hash.stats,
        #     'ImprintDirCache': self._dir_cache.stats,
        # }.items():
        #     YaMonEvent.send('EYaStats::' + name, 100.0 * stats.hit / stats.all if stats.all else 0.0)

    def _stats_json(self):
        return (
            self._rel_path.stats._json(),
            self._content_hash.stats._json(),
            # Iter files just helper cache
            # self._iter_files.stats._json(),
            self._dir_cache.stats._json(),
        )

    def use_change_list(self, file_name, quiet=False):
        """
        @type file_name: Union[str, ChangeList]
        """
        if self._change_list_applied:
            self.logger.warning("Change list already applied")
            if quiet:
                return
            raise ValueError("Change list already applied")

        if file_name is not None:
            if isinstance(file_name, ChangeList):
                change_list = file_name
            else:
                change_list = ChangeList(file_name)

            self._dir_cache.use_change_list(change_list.paths)
            self._iter_files.use_change_list(change_list.paths)
            self._content_hash.use_change_list(change_list.paths)
            self._rel_path.use_change_list(change_list.paths)

            self._change_list_applied = True

    # backward compatibility

    # DEPRECATED
    def _generate_detailed_imprints(self, root_path):
        for path in self._do_iter_files(root_path, yield_dirs=True):
            if os.path.isfile(path):
                yield path, "file", os.path.getsize(path), self(path)[path]
            else:
                # Get files and folders like os.listdir but only _is_build_[file,dir]
                files_count = len(tuple(self._do_iter_files(path, yield_dirs=True, do_recursively=False)))
                # We calculate `path`
                files_count -= 1
                yield path, "dir", files_count, self(path)[path]

    # DEPRECATED
    def generate_detailed_imprints(self, root_path):
        return sorted(self._generate_detailed_imprints(root_path), key=lambda r: r[0])

    # See https://st.yandex-team.ru/DEVTOOLS-7588
    def _do_filter_path(self, abs_path):
        paths = ("web/app_host/conf/graph_generator/tests/light", "apphost/conf/verticals")
        self._rel_path.warm_up(abs_path)
        rel_path = self._rel_path[abs_path]  # type: str
        for path in paths:
            if rel_path.startswith(path):
                return True
        return False

    # DEPRECATED
    def generate_path_imprint(self, paths, log=False):
        imprints_by_path = self(*paths)
        # For order
        imprints = (imprints_by_path[path] for path in sorted(paths))
        return self.combine_imprints(*imprints)
