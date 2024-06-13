import logging
import os
import tempfile

from six import iteritems

from multiprocessing import cpu_count
from multiprocessing.pool import ThreadPool

from library.python.fs import ensure_dir
import exts.yjson as json


class BaseMapper(object):
    """[T -> R] -> [[T] -> {T: R}]"""

    def __init__(self, f):
        self.f = f

    def __call__(self, *items):
        # TODO: __call__ call iter?
        raise NotImplementedError()

    # TODO: __iter__
    def __repr__(self):
        return "<{} for {}>".format(self.__class__.__name__, self.f)


class SimpleMapper(BaseMapper):
    def __call__(self, *items):
        return dict(zip(items, list(map(self.f, items))))


class ThreadPoolMapper(SimpleMapper):
    class _FakePool(object):
        @staticmethod
        def map(*args, **kwargs):
            return list(map(*args, **kwargs))

    pool = None

    @classmethod
    def _init_pool(cls):
        try:
            if cls.pool is None:
                cls.pool = ThreadPool(cpu_count())
        except RuntimeError:
            logging.exception("Can't create thread pool for ThreadPoolMapper; Using FakePool")
            cls.pool = cls._FakePool()

    def __init__(self, f):
        super(ThreadPoolMapper, self).__init__(f)
        self.pool = ThreadPool(cpu_count())

    def __call__(self, *items):
        # TODO: yieldable map?
        return dict(zip(items, self.pool.map(self.f, items)))


class Stats:
    def __init__(self, name):
        self.name = name
        self.hit = 0
        self.miss = 0

    @property
    def all(self):
        return self.hit + self.miss

    def clear(self):
        self.hit = 0
        self.miss = 0

    def _json(self):
        return {'name': self.name, 'hit': self.hit, 'miss': self.miss}

    def __repr__(self):
        return "<{}:{} hit {:.2f}% (hit: {}, miss: {})>".format(
            self.__class__.__name__, self.name, 100.0 * self.hit / self.all if self.all else 0, self.hit, self.miss
        )


class BaseCache(object):
    """Cache functions like [T] -> {T: R}"""

    def __init__(self, name, f, need_calc_check=None):
        self.name = name
        self.logger = logging.getLogger(__name__ + ":" + self.__class__.__name__ + "[" + name + "]")
        self.f = f
        self._additional_check = need_calc_check
        self._additional_check_enabled = True
        self._cache = {}

        self.stats = Stats(self.name)

    def disable_additional_check(self):
        self._additional_check_enabled = False
        self.logger.debug("Disable user check")

    def enable_additional_check(self):
        self._additional_check_enabled = True
        self.logger.debug("Enable user check")

    def __contains__(self, item):
        return item in self._cache

    def __getitem__(self, item):
        # TODO: Wait for ready?
        if item in self._cache:
            return self._cache[item]

        self.logger.warning("Use `warm_up` instead raw __getitem__: %s", item)

        self.warm_up(item)
        return self._cache[item]

    def _need_calc(self, item):
        return item not in self._cache or (
            self._additional_check_enabled
            and (self._additional_check is not None)
            and (self._additional_check(item, self._cache[item]))
        )

    def _update_cache(self, item, result):
        self._cache[item] = result
        return item, result

    def __call__(self, *items):
        return dict(self._do_calcs(items))

    def _do_calcs(self, items):
        results = self._apply(items)

        for item in items:
            if item in self._cache:
                yield item, self._cache[item]

        for item, result in iteritems(results):
            yield self._update_cache(item, result)

    def warm_up(self, *items):
        for item, result in iteritems(self._apply(items)):
            self._update_cache(item, result)

    def _apply(self, _items):
        items = set(_items)
        items_to_calculate = set(item for item in items if self._need_calc(item))

        self.stats.hit += len(items) - len(items_to_calculate)
        self.stats.miss += len(items_to_calculate)

        if items_to_calculate:
            return self.f(*items_to_calculate)
        else:
            return {}
        # TODO: future
        #       .iteritems() get ready items

    def clear(self):
        self._cache.clear()
        self.stats.clear()

    def load(self):
        pass

    def store(self):
        pass

    def _invalidate(self, *args):
        self.clear()

    def __repr__(self):
        return "<{}:{} ({} items) for {} {}>".format(
            self.__class__.__name__, self.name, len(self._cache), self.f, self.stats
        )

    def use_change_list(self, items):
        self._invalidate(items)
        self.disable_additional_check()


class BaseFileCache(BaseCache):
    """Cache which can store data into file.
    Can wrap other caches
    """

    def __init__(self, name, f, check=None, read=True, write=True):
        BaseCache.__init__(self, name, f, need_calc_check=check)
        self.read = read
        self.write = write

        self._cache_path = self._generate_source_path()

        self.load()

    def _generate_source_path(self):
        raise NotImplementedError()

    def _load_processor(self, data):
        return data

    def _store_processor(self, data):
        return data

    def _create_dirs(self, path_name):
        if not os.path.exists(path_name):
            ensure_dir(path_name)

    def load(self):
        if not self.read:
            self.logger.debug("read cache disabled, skip")
            return False

        try:
            if os.path.exists(self._cache_path):
                with open(self._cache_path, "rt") as f:
                    self._cache = dict(self._load_processor(json.load(f)))
                self.logger.debug("cache successfully read from %s", self._cache_path)
                self.logger.debug("cache read %d items", len(self._cache))
            else:
                self.logger.debug("not found cache file: %s", self._cache_path)
        except IOError:
            self.logger.exception("can't load cache file from: %s", self._cache_path)

    def store(self):
        if not self.write:
            self.logger.debug("cache write disabled, skip")
            return

        self.logger.debug("write the cache into %s", self._cache_path)
        self.logger.debug("cache wrote %d items", len(self._cache))

        try:
            tmp_file = tempfile.mktemp(dir=os.path.dirname(self._cache_path))
            with open(tmp_file, "w") as f:
                json.dump(dict(self._store_processor(self._cache)), f, sort_keys=True)

            os.rename(tmp_file, self._cache_path)

            self.logger.debug("cache successfully wrote")
        except IOError:
            self.logger.exception("can't save cache file into: %s", self._cache_path)

    def clear(self):
        super(BaseFileCache, self).clear()
        if isinstance(self.f, BaseCache):
            self.f.clear()

    def _invalidate(self, *args):
        super(BaseFileCache, self)._invalidate(*args)
        if isinstance(self.f, BaseCache):
            self.f._invalidate(*args)
