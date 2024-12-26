import contextlib
import logging
import six
import traceback

import devtools.ya.core.config


logger = logging.getLogger(__name__)


def _format_exception(exc):
    if six.PY2:
        return "{}".format(exc)
    else:
        return "".join(traceback.format_exception(exc))


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
        if devtools.ya.core.config.is_test_mode():
            log_func = logger.exception
        else:
            log_func = logger.warning
        if self._stack:
            func, suite = self._stack[-1]
            msg = "[[unimp]]<{}>[[rst]] {}".format(suite, _format_exception(exc))
            if func:
                func(msg=msg, path=suite.project_path, sub='SuiteConf')
            else:
                log_func("Broken suite: %s", msg)
        else:
            msg = "Failed to register suite error outside of suite context: {}".format(_format_exception(exc))
            if devtools.ya.core.config.is_test_mode():
                raise RuntimeError(msg)
            else:
                log_func(msg)


SuiteCtx = _SuiteCtx()
