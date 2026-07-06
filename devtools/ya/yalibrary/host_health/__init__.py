import logging
import os
import platform
import psutil
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum
from humanize import naturalsize
from typing import Self

from devtools.ya.core import report

MEMORY_THRESHOLDS = [80, 85] + list(range(90, 101, 1))
WATCH_INTERVAL = 0.05


logger = logging.getLogger(__name__)


class ReportCause(StrEnum):
    INIT = "init"
    MEM_THRESHOLD = "threshold"
    OOM = "oom"


class MemLimiter(StrEnum):
    RAM = "ram"
    CGROUP = "cgroup"


class HostHealth:
    def __init__(self):
        self._watcher = None

    def start_watcher(self, prefix: list[str], params) -> None:
        assert self._watcher is None
        self._watcher = HostWatcher(prefix, getattr(params, "build_threads", None))

    def stop_watcher(self) -> None:
        if self._watcher:
            self._watcher.stop()

    def get_host_state(self) -> dict | None:
        return self._watcher.get_host_state() if self._watcher else None


class HostWatcher:
    def __init__(self, prefix: list[str], build_threads: int):
        self._state = {
            "cpu_count": psutil.cpu_count(),
            "build_threads": build_threads,
            "host_platform": "-".join((platform.system(), platform.machine())).lower(),
            "prefix": prefix,
        }
        self._state_lock = threading.Lock()
        self._cpu_usage = CpuUsage()
        self._threshold_idx = -1
        self._cgroup_info = _CgroupMemoryInfo.get_cgroup_info()

        try:
            import app_ctx

            self._evlog_writer = app_ctx.evlog.get_writer('host_health')
        except (AttributeError, ImportError):
            self._evlog_writer = lambda *args, **kwargs: None

        self._watching_thread = StoppableThread(target=self._watch_func)

    def stop(self):
        self._watching_thread.stop(wait=True)
        self._cpu_usage.stop()

    def get_host_state(self):
        with self._state_lock:
            return self._state.copy()

    def _report_host_state(self, cause: ReportCause):
        state = {
            "cause": cause,
        }
        state.update(self._state)
        self._evlog_writer("host_state", **state)
        report.telemetry.report(report.ReportTypes.HOST_HEALTH, state, urgent=True)

    def _update_state(self):
        vm = psutil.virtual_memory()
        if self._cgroup_info:
            limiter = MemLimiter.CGROUP
            mem_limit = self._cgroup_info.get_mem_limit()
            mem_used = self._cgroup_info.get_mem_used()
        else:
            limiter = MemLimiter.RAM
            mem_limit = vm.total
            mem_used = vm.total - vm.available
        mem_perc = mem_used / mem_limit * 100.0
        cpu_usage = self._cpu_usage.get()

        mem_limit_info = {
            "limiter": limiter,
            "total": mem_limit,
            "used": mem_used,
            "used_perc": mem_perc,
        }

        while mem_perc >= MEMORY_THRESHOLDS[self._threshold_idx + 1]:
            self._threshold_idx += 1

        if self._threshold_idx >= 0:
            mem_limit_info["threshold"] = MEMORY_THRESHOLDS[self._threshold_idx]

        with self._state_lock:
            self._state.update(
                {
                    "mem_limit": mem_limit_info,
                    "ram": {
                        "total": vm.total,
                        "used": vm.total - vm.available,
                        "used_perc": vm.percent,
                    },
                    "cpu_usage_perc": {
                        "user": cpu_usage.user_perc,
                        "system": cpu_usage.system_perc,
                    },
                }
            )

    def _watch_func(self, stopped):
        prev_threshold_id = self._threshold_idx
        self._update_state()
        self._report_host_state(ReportCause.INIT)
        while not stopped():
            time.sleep(WATCH_INTERVAL)
            self._update_state()
            if prev_threshold_id != self._threshold_idx:
                self._report_host_state(ReportCause.MEM_THRESHOLD)
                prev_threshold_id = self._threshold_idx


class CpuUsage:
    _INTERVAL = 0.1

    @dataclass
    class Usage:
        user_perc: float
        system_perc: float

    def __init__(self):
        self._lock = threading.Lock()
        self._value: CpuUsage.Usage = None
        self._thread = StoppableThread(target=self._run)

    def get(self) -> "CpuUsage.Usage":
        while True:
            with self._lock:
                if self._value:
                    return self._value
            time.sleep(CpuUsage._INTERVAL / 2)

    def stop(self):
        self._thread.stop()

    def _run(self, stopped) -> None:
        while not stopped():
            v = psutil.cpu_times_percent(interval=self._INTERVAL)
            with self._lock:
                self._value = CpuUsage.Usage(v.user + getattr(v, "nice", 0), v.system)


class StoppableThread:
    def __init__(self, target, daemon=True, start=True):
        self._thread = threading.Thread(target=target, args=(self._stopped,), daemon=daemon)
        self._event = threading.Event()
        if start:
            self._thread.start()

    def start(self):
        self._thread.start()

    def stop(self, wait=False):
        self._event.set()
        if wait:
            self._thread.join()

    def _stopped(self):
        return self._event.is_set()


class _CgroupMemoryInfo:
    @dataclass(frozen=True)
    class _CgroupCfg:
        version: int
        limit_file: str
        stat_file: str
        used_memory_field: str
        mount_filter: Callable

    _PROC_CGROUP_PATH = "/proc/self/cgroup"
    _CGROUP_CFG_v1 = _CgroupCfg(
        version=1,
        limit_file="memory.limit_in_bytes",
        stat_file="memory.stat",
        used_memory_field="total_rss",
        mount_filter=lambda m: m.device == "cgroup" and "memory" in m.opts.split(","),
    )
    _CGROUP_CFG_v2 = _CgroupCfg(
        version=2,
        limit_file="memory.max",
        stat_file="memory.stat",
        used_memory_field="anon",
        mount_filter=lambda m: m.device == "cgroup2",
    )

    def __init__(self, used_memory_path: str, used_memory_field, memory_limit: int):
        self._used_memory_path = used_memory_path
        self._used_memory_field = used_memory_field
        self._memory_limit = memory_limit

    def get_mem_limit(self) -> int:
        return self._memory_limit

    def get_mem_used(self) -> int:
        with open(self._used_memory_path) as f:
            for line in f:
                line = line.rstrip()
                name, value = line.split()
                if name == self._used_memory_field:
                    return int(value)
        raise RuntimeError(f"'{self._used_memory_field}' not found in {self._used_memory_path}")

    @classmethod
    def get_cgroup_info(cls) -> Self | None:
        if platform.system() != "Linux":
            return None

        path_n_cfg = cls._get_memory_controller_path_and_config()
        if not path_n_cfg:
            return None
        cgroup_path, cgroup_cfg = path_n_cfg

        if result := cls._find_limit(cgroup_cfg.mount_filter, cgroup_path, cgroup_cfg.limit_file):
            path, limit = result
            logger.debug(
                "Cgroup v%d memory limit is on. Path=%s, limit=%d (%s)",
                cgroup_cfg.version,
                path,
                limit,
                naturalsize(limit, binary=True),
            )
            return _CgroupMemoryInfo(os.path.join(path, cgroup_cfg.stat_file), cgroup_cfg.used_memory_field, limit)

    @classmethod
    def _get_memory_controller_path_and_config(cls) -> tuple[str, _CgroupCfg] | None:
        if not os.path.exists(cls._PROC_CGROUP_PATH):
            return

        with open(cls._PROC_CGROUP_PATH) as f:
            v2_path = None
            for line in f:
                line = line.rstrip()
                id, controllers, path = line.split(":")
                if id == '0':
                    v2_path = path
                elif 'memory' in controllers:
                    return path.lstrip("/"), cls._CGROUP_CFG_v1
            if v2_path:
                return v2_path.lstrip("/"), cls._CGROUP_CFG_v2
            return

    @classmethod
    def _find_limit(cls, mount_filter: Callable, cgroup_path: str, limit_file: str) -> tuple[str, int] | None:
        mount_path = cls._get_cgroup_mount(mount_filter)
        if mount_path is None:
            return

        max_mem = psutil.virtual_memory().total
        full_path = os.path.join(mount_path, cgroup_path)
        while len(full_path) >= len(mount_path):
            limit_path = os.path.join(full_path, limit_file)
            if os.path.exists(limit_path):
                with open(limit_path) as f:
                    raw_limit_value = f.read().rstrip()
                    if raw_limit_value.isdigit():
                        if 0 < (limit := int(raw_limit_value)) < max_mem:
                            return full_path, limit
            full_path = os.path.dirname(full_path)
        return None

    @classmethod
    def _get_cgroup_mount(cls, mount_filter: Callable) -> str | None:
        for mount in psutil.disk_partitions(all=True):
            if mount_filter(mount):
                return mount.mountpoint
