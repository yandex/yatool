import datetime
import logging

logger = logging.getLogger(__name__)


DATETIME_FMT = "%Y-%m-%d %H:%M:%S"
TIME_FMT = "%H:%M:%S"


def _pretty(timestamp, fmt):
    if isinstance(timestamp, (int, float)):
        timestamp = datetime.datetime.fromtimestamp(timestamp)
        timestamp = timestamp.strftime(fmt)
    else:
        timestamp = "?"
    return timestamp


def pretty_date(timestamp):
    try:
        return _pretty(timestamp, DATETIME_FMT)
    except Exception as e:
        logger.error("While converting date `%s`: %s", timestamp, e)
        return "?"


def pretty_time(timestamp):
    try:
        return _pretty(timestamp, TIME_FMT)
    except Exception as e:
        logger.info("While converting time `%s`: %s", timestamp, e)
        return "?"


def pretty_delta(seconds):
    try:
        if seconds is not None:
            seconds = float(seconds)

            td = datetime.timedelta(seconds=seconds)

            s = str(td)
            if '.' in s:
                _ = s.split('.')
                _[1] = _[1][:2]
                s = ".".join(_)

            return s
        else:
            return "?"
    except Exception as e:
        logger.error("While converting delta `%s`: %s", seconds, e)
        return "?"
