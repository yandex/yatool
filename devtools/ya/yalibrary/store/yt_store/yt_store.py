import json
import logging
import os

from devtools.ya.core import stage_tracer
from devtools.ya.core import report
from yalibrary.store.dist_store import DistStore
from . import xx_client
from .xx_client import YtStoreError  # noqa


YT_CACHE_NO_DATA_CODEC = "no_data"


logger = logging.getLogger(__name__)


class YtStore(xx_client.YtStoreImpl, DistStore):
    def __init__(
        self,
        proxy: str,
        data_dir: str,
        token: str | None = None,
        proxy_role: str | None = None,
        readonly=True,
        check_size=False,
        max_cache_size: str | int | None = None,
        ttl: int | None = None,
        name_re_ttls: dict[str, int] | None = None,
        max_file_size: int = 0,
        probe_before_put=False,
        probe_before_put_min_size=0,
        retry_time_limit: float | None = None,
        operation_pool: str | None = None,
        init_timeout: float | None = None,
        prepare_timeout: float | None = None,
        crit_level: str | None = None,
        gsid: str | None = None,
        stager: stage_tracer.StageTracer.GroupStageTracer | None = None,
        **kwargs
    ):
        xx_client.YtStoreImpl.__init__(
            self,
            proxy,
            data_dir,
            token=token,
            proxy_role=proxy_role,
            readonly=readonly,
            check_size=check_size,
            max_cache_size=max_cache_size,
            ttl_hours=ttl if ttl else None,
            name_re_ttls=name_re_ttls,
            operation_pool=operation_pool,
            retry_time_limit=retry_time_limit,
            init_timeout=init_timeout,
            prepare_timeout=prepare_timeout,
            probe_before_put=probe_before_put,
            probe_before_put_min_size=probe_before_put_min_size,
            crit_level=crit_level,
            gsid=gsid,
            stager=stager,
        )
        DistStore.__init__(
            self,
            name='yt-store',
            stats_name='yt_store_stats',
            tag='YT',
            readonly=readonly,
            max_file_size=max_file_size,
        )

    def stats(self, execution_log, evlog_writer):
        metrics = xx_client.YtStoreImpl.get_metrics(self)
        for tag, val in metrics.timers.items():
            self._timers[tag] += val
        for tag, intervals in metrics.time_intervals.items():
            self._time_intervals[tag].extend(intervals)
        for tag, val in metrics.counters.items():
            self._counters[tag] += val
        for tag, val in metrics.failures.items():
            self._failures[tag] += val
        for tag, val in metrics.data_size.items():
            self._data_size[tag] += val
        self._cache_hit = {'requested': metrics.requested, 'found': metrics.found}

        DistStore.stats(self, execution_log, evlog_writer)
        stat = {
            "time_to_first_recv_meta": metrics.time_to_first_recv_meta,
            "time_to_first_call_has": metrics.time_to_first_call_has,
            "failed_during_build": self.disabled(),
            "failed_during_setup": bool(metrics.time_to_first_recv_meta),
        }
        report.telemetry.report('{}-{}'.format(self._stats_name, 'additional-info'), stat)


class YndexerYtStore(YtStore):
    YDX_PB2_EXT = '.ydx.pb2.yt'

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def fits(self, node):
        outputs = node["outputs"] if isinstance(node, dict) else node.outputs
        return any(out.endswith(YndexerYtStore.YDX_PB2_EXT) for out in outputs)

    def _do_put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        assert codec == YT_CACHE_NO_DATA_CODEC
        forced_node_size = None

        size_file = list(filter(lambda x: x.endswith(self.YDX_PB2_EXT), files))
        if not size_file:
            logger.error('Sizefile not found. Real output size will be placed into dist cache.')
        elif len(size_file) > 1:
            logger.error('Too many sizefiles found. Real output size will be placed into dist cache.')
        else:
            try:
                with open(size_file[0]) as f:
                    stats = json.load(f)
                    forced_node_size = stats['bytes_to_upload']
            except Exception as e:
                logger.error(
                    'Can\'t read data size from sizefile: {}. Real output size will be placed into dist cache.'.format(
                        e
                    )
                )

        if forced_node_size is None:
            forced_node_size = sum(os.lstat(f).st_size for f in files)

        return super()._do_put(
            self_uid,
            uid,
            root_dir,
            files,
            codec=codec,
            cuid=cuid,
            forced_size=forced_node_size,
        )
