__all__ = ['AppCtx']


import contextlib2
import logging
import time
import sys


logger = logging.getLogger(__name__)


def get_app_ctx():
    # type: () -> AppCtx
    if 'app_ctx' in sys.modules:
        import app_ctx as ctx
    else:
        ctx = AppCtx()
        sys.modules['app_ctx'] = ctx
    return ctx


# Subclassing a private class definitely doesn't look like a good idea, but neither does the copying of it's code
class GeneratorModuleWrapper(contextlib2._GeneratorContextManager):
    def __init__(self, name, gen, stager):
        self._name = name
        self._stager = stager
        self._stage_name_enter = 'module-lifecycle-{}-enter'.format(self._name)
        self._stage_name_exit = 'module-lifecycle-{}-exit'.format(self._name)
        super(GeneratorModuleWrapper, self).__init__(lambda: gen, [], {})

    def __enter__(self):
        start_time = time.time()
        try:
            value = super(GeneratorModuleWrapper, self).__enter__()
        except Exception as e:
            logger.warning('Exception during module "%s" initialization: %s', self._name, e)
            from traceback import format_exc

            logger.debug("%s", format_exc())
            raise
        else:
            stop_time = time.time()
            logger.debug('Module "%s" initialized in %f', self._name, stop_time - start_time)
            if self._stager is not None:
                stage = self._stager.start(self._stage_name_enter, start_time=start_time)
                stage.finish(finish_time=stop_time)
            return value

    def __exit__(self, type, value, traceback):
        start_time = time.time()
        try:
            value = super(GeneratorModuleWrapper, self).__exit__(type, value, traceback)
        except Exception as e:
            logger.warning('Exception during module "%s" stopping: %s', self._name, e)
            from traceback import format_exc

            logger.debug("%s", format_exc())
        else:
            stop_time = time.time()
            logger.debug('Module "%s" stopped in %f', self._name, stop_time - start_time)
            if self._stager is not None:
                stage = self._stager.start(self._stage_name_exit, start_time=start_time)
                stage.finish(finish_time=stop_time)
            return value


class AppCtxStack(contextlib2.ExitStack):
    def __init__(self, kv, modules, stager):
        super(AppCtxStack, self).__init__()
        self._kv = kv
        self._prev = {}
        self._modules = modules
        self._stager = stager

    def __enter__(self):
        logger.debug('Add %s to ctx %s', self._modules, self._kv.keys())
        for k, v in self._modules:
            if k in self._kv:
                self._prev[k] = self._kv[k]
            self._kv[k] = self.enter_context(GeneratorModuleWrapper(k, v, self._stager))

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

    def configure(self, modules, stager=None):
        return AppCtxStack(self._kv, modules, stager)

    def __getattr__(self, item):
        try:
            return self._kv[item]
        except KeyError:
            raise ContextConfigurationError(
                "Application context wasn't configured to handle '{}' module. Available modules is: [{}]".format(
                    item, ', '.join(sorted(self._kv.keys()))
                )
            )
