__all__ = ['AppCtx']


import contextlib2
import logging
import time
import sys


logger = logging.getLogger(__name__)


def get_app_ctx():
    if 'app_ctx' in sys.modules:
        import app_ctx as ctx
    else:
        ctx = AppCtx()
        sys.modules['app_ctx'] = ctx
    return ctx


# Subclassing a private class definitely doesn't look like a good idea, but neither does the copying of it's code
class GeneratorModuleWrapper(contextlib2._GeneratorContextManager):
    def __init__(self, name, gen):
        self._name = name
        super(GeneratorModuleWrapper, self).__init__(lambda: gen, [], {})

    def __enter__(self):
        st = time.time()
        value = super(GeneratorModuleWrapper, self).__enter__()
        logger.debug('Module "%s" initialized in %f', self._name, time.time() - st)
        return value

    def __exit__(self, type, value, traceback):
        st = time.time()
        try:
            return super(GeneratorModuleWrapper, self).__exit__(type, value, traceback)
        except Exception as e:
            logger.warning('Exception during module "%s" stopping: %s', self._name, e)
            from traceback import format_exc

            logger.debug("%s", format_exc())
        finally:
            logger.debug('Module "%s" stopped in %f', self._name, time.time() - st)


class AppCtxStack(contextlib2.ExitStack):
    def __init__(self, kv, modules):
        super(AppCtxStack, self).__init__()
        self._kv = kv
        self._prev = {}
        self._modules = modules

    def __enter__(self):
        logger.debug('Add %s to ctx %s', self._modules, self._kv.keys())
        for k, v in self._modules:
            if k in self._kv:
                self._prev[k] = self._kv[k]
            self._kv[k] = self.enter_context(GeneratorModuleWrapper(k, v))

        return self

    def __exit__(self, *exc_details):
        try:
            return super(AppCtxStack, self).__exit__(*exc_details)
        finally:
            for k, _ in reversed(self._modules):
                if k in self._prev:
                    self._kv[k] = self._prev[k]
                else:
                    del self._kv[k]
            logger.debug('Restored ctx %s', self._kv.keys())


class ContextConfigurationError(AttributeError):
    pass


class AppCtx(object):
    __slots__ = ('_kv',)  # to prevent from adding attributes from the outside

    def __init__(self, **kwargs):
        self._kv = kwargs

    def configure(self, modules):
        return AppCtxStack(self._kv, modules)

    def __getattr__(self, item):
        try:
            return self._kv[item]
        except KeyError:
            raise ContextConfigurationError(
                "Application context wasn't configured to handle '{}' module. Available modules is: [{}]".format(
                    item, ', '.join(sorted(self._kv.keys()))
                )
            )
