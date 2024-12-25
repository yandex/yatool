import logging
import sys

logger = logging.getLogger(__name__)


class PluginMap(object):
    def __init__(self):
        self._plugins = {}
        self._loaded_plugins = {}

    def add(self, name, loader):
        self._plugins[name] = loader

    def names(self):
        return self._plugins.keys()

    def get(self, item):
        def loader():
            if item not in self._loaded_plugins:
                self._loaded_plugins[item] = self._plugins[item]()
            return self._loaded_plugins[item]

        return loader

    def __add__(self, other):
        res = PluginMap()
        res._plugins.update(self._plugins)
        res._plugins.update(other._plugins)
        res._loaded_plugins.update(other._loaded_plugins)
        res._loaded_plugins.update(other._loaded_plugins)
        return res


def explore_plugins(loader_hook, suffix):
    def make_loader(path):
        def loader():
            logger.debug('Load {0}'.format(path))
            import importlib  # since 2.7

            return importlib.import_module(path)

        return lambda: loader_hook(loader)

    result = PluginMap()

    for m in sys.extra_modules:
        if m.startswith('devtools.ya.handlers.') and m.endswith('.__init__'):
            m = m.replace('.__init__', '')
            parts = m.split('.')
            if len(parts) != 4:
                continue
            full_name = parts[-1]
            name = full_name.replace(suffix, '').replace('_', '-')
            result.add(name, make_loader(m))

    return result
