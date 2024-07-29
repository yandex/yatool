import contextlib
import logging
import time


logger = logging.getLogger(__name__)


class YaMonEvent(object):
    _evlog_writer = None

    @staticmethod
    def send(name, value):
        # PY3 def send(name: str, value: bool | int | float) -> None:
        if not YaMonEvent._evlog_writer:
            if YaMonEvent._evlog_writer is not None:
                return  # Already tried get evlog, absent, do nothing

            try:
                import app_ctx

                YaMonEvent._evlog_writer = app_ctx.evlog.get_writer('ya')
            except (ImportError, AttributeError):
                logger.debug('app_ctx.evlog not found')
                YaMonEvent._evlog_writer = False
                return

        # Event format see TMonitoringStat at devtools/ymake/diag/trace.ev
        # Name format is string representation of enum items in devtools/ymake/diag/stats_enum.h, here use same strings for generality
        if isinstance(value, bool):
            YaMonEvent._send_evlog(
                Name=name,
                Type='bool',
                BoolValue='true' if value else 'false',
            )
        elif isinstance(value, float):
            YaMonEvent._send_evlog(
                Name=name,
                Type='double',
                DoubleValue=str(value),
            )
        elif isinstance(value, int):
            YaMonEvent._send_evlog(
                Name=name,
                Type='int',
                IntValue=str(value),
            )
        else:
            logger.warn("Skip unknown type YaMonEvent value with name '%s'", name)

    @staticmethod
    def _send_evlog(**kwargs):
        YaMonEvent._evlog_writer('NEvent.TMonitoringStat', **kwargs)


@contextlib.contextmanager
def monitoring_scope(name):
    # type: (str) -> types.GeneratorType
    start = time.time()
    try:
        yield None
    finally:
        YaMonEvent.send(name, time.time() - start)
