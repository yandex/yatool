import enum
import os
import six
import time

import humanfriendly
from devtools.ya.core import report
from exts.timer import AccumulateTime

import logging

logger = logging.getLogger(__name__)


class Status(enum.Enum):
    OK = 1
    FAILED = 2
    SKIPPED = 3

    @property
    def ok(self):
        return self.value == Status.OK.value

    @property
    def skipped(self):
        return self.value == Status.SKIPPED.value


class DistStore(object):
    def __init__(self, name, stats_name, tag, readonly, max_file_size=0, fits_filter=None, heater_mode=False):
        self._readonly = readonly
        metrics = ['has', 'put', 'get', 'get-meta', 'probe-meta-before-put']
        self._timers = {m: 0 for m in metrics}
        self._time_intervals = {m: [] for m in metrics}
        self._counters = {m: 0 for m in metrics + ['skip-put']}
        self._failures = {m: 0 for m in metrics}
        self._data_size = {'put': 0, 'get': 0, 'skip-put': 0}
        self._cache_hit = {'requested': 0, 'found': 0}
        self._meta = {}
        self._name = name
        self._stats_name = stats_name
        self._tag = tag
        self._fits_filter = fits_filter
        self._exclude_filter = self._gen_exclude_filter(max_file_size)
        self._heater_mode = heater_mode

    def _inc_time(self, x, tag):
        cur_time = time.time()
        self._timers[tag] += x
        self._time_intervals[tag].append((cur_time - x, cur_time))
        self._counters[tag] += 1

    def _count_failure(self, tag):
        self._failures[tag] += 1

    def _inc_data_size(self, size, tag):
        self._data_size[tag] += size

    def _gen_exclude_filter(self, limit):
        if limit:

            def exclude_filter(files):
                # There is no point in uploading all the files if at least one exceeds the limit
                for filename in files:
                    size = os.stat(filename).st_size
                    if size > limit:
                        return "{} ({}) exceeds max file size limit".format(
                            os.path.basename(filename), humanfriendly.format_size(size, binary=True)
                        )

            return exclude_filter
        else:
            return lambda x: False

    def fits(self, node):
        raise NotImplementedError()

    def prepare(self, *args, **kwargs):
        return

    def _do_has(self, uid):
        raise NotImplementedError()

    def has(self, *args, **kwargs):
        with AccumulateTime(lambda x: self._inc_time(x, 'has')):
            return self._do_has(*args, **kwargs)

    def _do_put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        raise NotImplementedError()

    def put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        if self._exclude_filter:
            reason = self._exclude_filter(files)
            if reason:
                logger.debug("Skipping uploading of %s: %s", uid, reason)
                return Status.SKIPPED

        with AccumulateTime(lambda x: self._inc_time(x, 'put')):
            if self._do_put(self_uid, uid, root_dir, files, codec, cuid):
                return Status.OK
            return Status.FAILED

    def _do_try_restore(self, uid, into_dir, filter_func=None):
        raise NotImplementedError()

    def try_restore(self, *args, **kwargs):
        with AccumulateTime(lambda x: self._inc_time(x, 'get')):
            return self._do_try_restore(*args, **kwargs)

    def avg_compression_ratio(self):
        raise NotImplementedError()

    def readonly(self):
        return self._readonly

    def _get_real_time(self, key):
        if not self._time_intervals[key]:
            return 0.0
        # Merge overlapped intervals
        BEG, END = 0, 1
        intervals = sorted(self._time_intervals[key])
        merged = intervals[:1]
        for ti in intervals[1:]:
            last_ti = merged[-1]
            if last_ti[END] >= ti[BEG]:
                b = min(ti[BEG], last_ti[BEG])
                e = max(ti[END], last_ti[END])
                merged[-1] = (b, e)
            else:
                merged.append(ti)

        return sum(ti[END] - ti[BEG] for ti in merged)

    def stats(self, execution_log, evlog_writer):
        for k, v in six.iteritems(self._data_size):
            stat_dict = {'data_size': v, 'type': self._name}
            report.telemetry.report('{}-{}-data-size'.format(self._stats_name, k), stat_dict)
            execution_log['$({}-{}-data-size)'.format(self._name, k)] = stat_dict
        execution_log['$({}-cache-hit)'.format(self._name)] = self._cache_hit

        real_times = {}
        for k, v in six.iteritems(self._timers):
            real_times[k] = real_time = self._get_real_time(k)

            stat_dict = {
                'count': self._counters[k],
                'failures': self._failures[k],
                'prepare': '',
                'timing': (0, real_time),
                'total_time': True,
                'type': self._name,
                'real_time': real_time,
            }
            report.telemetry.report('{}-{}'.format(self._stats_name, k), stat_dict)
            execution_log["$({}-{})".format(self._name, k)] = stat_dict
        if evlog_writer:
            stats = {
                'cache_hit': self._cache_hit,
                'put': {
                    'count': self._counters['put'],
                    'data_size': self._data_size['put'],
                    'time': real_times['put'],
                },
                'get': {
                    'count': self._counters['get'],
                    'data_size': self._data_size['get'],
                    'time': real_times['get'],
                },
            }
            if real_times['probe-meta-before-put']:
                stats['probe_before_put'] = {
                    'skip_count': self._counters['skip-put'],
                    'skip_data_size': self._data_size['skip-put'],
                    'time': real_times['probe-meta-before-put'],
                }
            evlog_writer('stats', **stats)

    def tag(self):
        if self._tag is None:
            raise ValueError()
        return self._tag
