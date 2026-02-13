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

    def start_watcher(self, prefix: list[str], params):
        assert self._watcher is None
        self._watcher = HostWatcher(prefix, getattr(params, "build_threads", None))

    def stop_watcher(self):
        if self._watcher:
            self._watcher.stop()


class HostWatcher:
    def __init__(self, prefix: list[str], build_threads: int):
        self._prefix = prefix
        self._build_threads = build_threads
        self._host_platform = "-".join((platform.system(), platform.machine())).lower()

        try:
            import app_ctx

            self._evlog_writer = app_ctx.evlog.get_writer('host_health')
        except (AttributeError, ImportError):
            self._evlog_writer = lambda *args, **kwargs: None

        self._cpu_usage = CpuUsage()
        self._watching_thread = StoppableThread(target=self._watch_func)

    def stop(self):
        self._watching_thread.stop(wait=True)
        self._cpu_usage.stop()

    def _report_host_state(self, cause: ReportCause, state: dict | None = None):
        state = state or {}
        vm = psutil.virtual_memory()
        cpu_usage = self._cpu_usage.get()
        state.update(
            {
                "cause": cause,
                "prefix": self._prefix,
                "ram": {
                    "total": vm.total,
                    "used": vm.total - vm.available,
                    "used_perc": vm.percent,
                },
                "cpu_usage_perc": {
                    "user": cpu_usage.user_perc,
                    "system": cpu_usage.system_perc,
                },
                "cpu_count": psutil.cpu_count(),
                "build_threads": self._build_threads,
                "host_platform": self._host_platform,
            }
        )
        self._evlog_writer("host_state", **state)
        report.telemetry.report(report.ReportTypes.HOST_HEALTH, state, urgent=True)

    def _watch_func(self, stopped):
        next_threshold_idx = 0
        self._report_host_state(ReportCause.INIT)
        while not stopped():
            # TODO Add cgroup support
            vm = psutil.virtual_memory()
            mem_limit = vm.total
            mem_used = vm.total - vm.available
            mem_perc = mem_used / mem_limit * 100.0

            current_threshold = None
            while mem_perc >= MEMORY_THRESHOLDS[next_threshold_idx]:
                current_threshold = MEMORY_THRESHOLDS[next_threshold_idx]
                next_threshold_idx += 1

            if current_threshold is not None:
                state = {
                    "mem_limit": {
                        "threshold": current_threshold,
                        "limiter": MemLimiter.RAM,
                        "total": mem_limit,
                        "used": mem_used,
                        "used_perc": mem_perc,
                    }
                }
                self._report_host_state(ReportCause.MEM_THRESHOLD, state)
            time.sleep(WATCH_INTERVAL)


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
                self._value = CpuUsage.Usage(v.user + v.nice, v.system)


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
