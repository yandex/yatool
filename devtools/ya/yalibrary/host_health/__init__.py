import platform
import psutil
import threading
import time
from dataclasses import dataclass
from enum import StrEnum

from devtools.ya.core import report

MEMORY_THRESHOLDS = [80, 85] + list(range(90, 101, 1))
WATCH_INTERVAL = 0.05


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

    def get_host_state(self) -> dict:
        return self._watcher.get_host_state()


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
        # TODO Add cgroup support for memory limits
        vm = psutil.virtual_memory()
        limiter = MemLimiter.RAM
        cpu_usage = self._cpu_usage.get()
        mem_limit = vm.total
        mem_used = vm.total - vm.available
        mem_perc = mem_used / mem_limit * 100.0

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
