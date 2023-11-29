import contextlib
import logging

import core.config


logger = logging.getLogger(__name__)


class _SuiteCtx:
    def __init__(self):
        self._stack = []

    def __call__(self, add_error_func, suite):
        return self._get_cm(add_error_func, suite)

    @contextlib.contextmanager
    def _get_cm(self, func, suite):
        self._stack.append((func, suite))
        try:
            yield
        except Exception:
            self._stack.pop()
            raise

    def add_error(self, exc):
        if self._stack:
            func, suite = self._stack[-1]
            msg = "[[unimp]]<{}>[[rst]] {}".format(suite, exc)
            if func:
                func(msg=msg, path=suite.project_path, sub='SuiteConf')
            else:
                logger.warning("Broken suite: %s", msg)
        else:
            msg = "Failed to register suite error outside of suite context: {}".format(exc)
            if core.config.is_test_mode():
                raise RuntimeError(msg)
            else:
                logger.warning(msg)


SuiteCtx = _SuiteCtx()
