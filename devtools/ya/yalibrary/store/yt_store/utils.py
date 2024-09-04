import contextlib
import datetime
import logging
import sys

import yt.wrapper.yson as ywy
import humanfriendly
import six

import yalibrary.store.yt_store.consts as consts

logger = logging.getLogger(__name__)


def pivot_keys(tablets_count):
    step = consts.YT_CACHE_HASH_SPACE // tablets_count
    return [[]] + [[ywy.YsonUint64(i)] for i in six.moves.xrange(step, consts.YT_CACHE_HASH_SPACE, step)]


def make_table_attrs(schema, tablets_count, **kwargs):
    attrs = {
        'schema': schema,
        'dynamic': 'true',
        'pivot_keys': pivot_keys(tablets_count),
        'disable_tablet_balancer': 'true',
        'atomicity': 'none',
        'compression_codec': 'none',
        'replication_factor': 3,
    }
    attrs.update(kwargs)
    return attrs


def time_to_microseconds(t):
    return int(t * 1e6)


def microseconds_to_str(t):
    return datetime.datetime.utcfromtimestamp(t / 1e6).strftime('%Y-%m-%dT%H:%M:%S UTC')


def microsecond_interval_to_str(t):
    ts = int(t / 1e6)
    s = ts % 60
    m = (ts // 60) % 60
    h = (ts // 3600) % 24
    d = ts // 86400
    return '{}d {:02d}h {:02d}m {:02d}s'.format(d, h, m, s)


def human_size(size):
    return humanfriendly.format_size(size, binary=True)


def check_cancel_state():
    if 'app_ctx' in sys.modules:
        # raises exception if application is stopped
        sys.modules['app_ctx'].state.check_cancel_state()


class DummyStager:
    @contextlib.contextmanager
    def scope(self, *args, **kwargs):
        yield None
