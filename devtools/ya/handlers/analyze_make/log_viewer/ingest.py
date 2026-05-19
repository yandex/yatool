from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path
import time

from devtools.ya.handlers.analyze_make.log_viewer.parsers import parse_log, parse_log_parallel
from devtools.ya.handlers.analyze_make.log_viewer.store import LogRepository, StoreIngestTimings


@dataclass(slots=True)
class LogProcessingStats:
    path: str
    records: int
    elapsed_sec: float
    size_bytes: int | None
    parse_sec: float | None = None
    sqlite_insert_sec: float | None = None


def ingest_log_file(store: LogRepository, path: str, *, log_workers: int = 1) -> LogProcessingStats:
    parser = parse_log_parallel(path, log_workers) if log_workers > 1 else parse_log(path)
    timed_parser = _TimedIterator(parser)
    timings = StoreIngestTimings()
    started = time.perf_counter()
    records = store.ingest(path, timed_parser, timings=timings)
    elapsed = time.perf_counter() - started
    return LogProcessingStats(
        path=path,
        records=records,
        elapsed_sec=elapsed,
        size_bytes=_file_size(path),
        parse_sec=timed_parser.elapsed_sec,
        sqlite_insert_sec=timings.sqlite_insert_sec,
    )


def benchmark_log_file(path: str, *, log_workers: int = 1) -> LogProcessingStats:
    parser = parse_log_parallel(path, log_workers) if log_workers > 1 else parse_log(path)
    started = time.perf_counter()
    records = sum(1 for _ in parser)
    elapsed = time.perf_counter() - started
    return LogProcessingStats(
        path=path,
        records=records,
        elapsed_sec=elapsed,
        size_bytes=_file_size(path),
    )


def format_ingest_stats(stats: LogProcessingStats) -> str:
    details = _format_ingest_details(stats)
    return (
        f"  [log] {stats.path}: {stats.records} records, {_fmt_size_or_unknown(stats.size_bytes)} "
        f"in {stats.elapsed_sec:.2f}s ({details})"
    )


def format_benchmark_stats(stats: LogProcessingStats, *, log_workers: int = 1) -> str:
    workers_suffix = f", workers={log_workers}" if log_workers > 1 else ""
    return (
        f"[bench:log{workers_suffix}] {stats.path}: {stats.records} records, {_fmt_size_or_unknown(stats.size_bytes)} "
        f"in {stats.elapsed_sec:.2f}s{format_throughput(stats)}"
    )


def format_throughput(stats: LogProcessingStats) -> str:
    text = _format_throughput(stats)
    return f" ({text})" if text else ""


class _TimedIterator[T](Iterator[T]):
    def __init__(self, source: Iterable[T]) -> None:
        self._source = iter(source)
        self.elapsed_sec = 0.0

    def __iter__(self) -> Iterator[T]:
        return self

    def __next__(self) -> T:
        started = time.perf_counter()
        try:
            return next(self._source)
        finally:
            self.elapsed_sec += time.perf_counter() - started


def _format_ingest_details(stats: LogProcessingStats) -> str:
    details: list[str] = []
    measured_sec = 0.0

    if stats.parse_sec is not None:
        details.append(f"parse {stats.parse_sec:.2f}s")
        measured_sec += stats.parse_sec
    if stats.sqlite_insert_sec is not None:
        details.append(f"SQLite insert {stats.sqlite_insert_sec:.2f}s")
        measured_sec += stats.sqlite_insert_sec

    other_sec = stats.elapsed_sec - measured_sec
    if other_sec > 0.005:
        details.append(f"other {other_sec:.2f}s")

    throughput = _format_throughput(stats)
    if throughput:
        details.append(throughput)

    return "; ".join(details)


def _format_throughput(stats: LogProcessingStats) -> str:
    if stats.elapsed_sec <= 0:
        return ""
    records_s = stats.records / stats.elapsed_sec
    if stats.size_bytes is None:
        return f"{records_s:.0f} rec/s"
    mib_s = stats.size_bytes / (1024 * 1024) / stats.elapsed_sec
    return f"{records_s:.0f} rec/s, {mib_s:.2f} MB/s"


def _file_size(path: str) -> int | None:
    try:
        return Path(path).stat().st_size
    except OSError:
        return None


def _fmt_size_or_unknown(size_bytes: int | None) -> str:
    return "?" if size_bytes is None else _fmt_size(size_bytes)


def _fmt_size(n: int) -> str:
    size = float(n)
    for unit in ("B", "KB", "MB", "GB", "TB", "PB"):
        if size < 1024 or unit == "PB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.2f} {unit}"
        size /= 1024.0
    return f"{size:.2f} PB"
