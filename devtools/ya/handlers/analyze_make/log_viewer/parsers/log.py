from concurrent.futures import ProcessPoolExecutor
import datetime
import re
import multiprocessing
import os
from typing import Iterator
import time

from devtools.ya.handlers.analyze_make.log_viewer.models import LogRecord

LOG_LINE_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3}) (\w+) \(([^)]+)\) \[([^\]]+)\] (.*)$")
_READ_BUFFER_SIZE = 1024 * 1024
_PARALLEL_JOBS_PER_WORKER = 4


def parse_log(path: str) -> Iterator[LogRecord]:
    current: LogRecord | None = None
    current_raw_parts: list[str] | None = None
    log_line_match = LOG_LINE_RE.match
    looks_like_log_record_start = _looks_like_log_record_start
    finalize = _finalize
    parse_timestamp_sec = _parse_timestamp_sec
    log_record = LogRecord
    timestamp_cache: dict[str, float | None] = {}

    with open(path, "r", encoding="utf-8", errors="replace", buffering=_READ_BUFFER_SIZE) as fh:
        for line_no, raw_line in enumerate(fh, 1):
            line = raw_line.rstrip("\n")
            m = log_line_match(line) if looks_like_log_record_start(line) else None
            if m:
                if current is not None:
                    finalize(current, current_raw_parts)
                    yield current
                ts_text, level, module, thread, _message = m.groups()
                current = log_record(
                    id=0,
                    file_path=path,
                    raw=line,
                    timestamp_sec=parse_timestamp_sec(ts_text, timestamp_cache),
                    timestamp_text=ts_text,
                    line_number=line_no,
                    level=level,
                    module=module,
                    thread=thread,
                )
                current_raw_parts = None
            elif current is not None:
                if current_raw_parts is None:
                    current_raw_parts = [current.raw]
                current_raw_parts.append(line)
            elif line.strip():
                # outlier-строка до первого матча — отдельная запись с минимумом полей
                yield log_record(
                    id=0,
                    file_path=path,
                    raw=line,
                    line_number=line_no,
                    message_length=len(line),
                )

        if current is not None:
            finalize(current, current_raw_parts)
            yield current


def parse_log_parallel(path: str, workers: int) -> Iterator[LogRecord]:
    workers = max(1, workers)
    if workers == 1:
        yield from parse_log(path)
        return

    chunks = _split_log_file(path, workers * _PARALLEL_JOBS_PER_WORKER)
    if len(chunks) <= 1:
        yield from parse_log(path)
        return

    context = _multiprocessing_context()
    with ProcessPoolExecutor(max_workers=workers, mp_context=context) as pool:
        for records in pool.map(_parse_log_chunk_worker, chunks):
            yield from records


def _finalize(
    rec: LogRecord,
    raw_parts: list[str] | None = None,
) -> None:
    if raw_parts:
        rec.raw = "\n".join(raw_parts)
    rec.message_length = len(rec.raw)


def _split_log_file(path: str, requested_chunks: int) -> list[tuple[str, int, int, int]]:
    file_size = os.path.getsize(path)
    if file_size <= 0:
        return []

    requested_chunks = max(1, requested_chunks)
    starts = [0]
    for i in range(1, requested_chunks):
        boundary = _find_next_log_record_boundary(path, file_size * i // requested_chunks, file_size)
        if 0 < boundary < file_size and boundary > starts[-1]:
            starts.append(boundary)
    starts.append(file_size)

    start_line_numbers = _line_numbers_for_offsets(path, starts[:-1])
    return [
        (path, starts[i], starts[i + 1], start_line_numbers[starts[i]])
        for i in range(len(starts) - 1)
        if starts[i] < starts[i + 1]
    ]


def _find_next_log_record_boundary(path: str, offset: int, file_size: int) -> int:
    if offset <= 0:
        return 0
    if offset >= file_size:
        return file_size

    with open(path, "rb", buffering=_READ_BUFFER_SIZE) as fh:
        fh.seek(offset)
        # If offset points into a line, skip the partial line. If it happens to
        # point exactly to a record start, the previous chunk will simply become
        # a little larger and include that record.
        fh.readline()
        while True:
            pos = fh.tell()
            if pos >= file_size:
                return file_size
            line = fh.readline()
            if not line:
                return file_size
            if _looks_like_log_record_start_bytes(line):
                return pos


def _line_numbers_for_offsets(path: str, offsets: list[int]) -> dict[int, int]:
    targets = sorted(set(offsets))
    result: dict[int, int] = {}
    line_no = 1
    pos = 0

    with open(path, "rb", buffering=_READ_BUFFER_SIZE) as fh:
        for target in targets:
            while pos < target:
                data = fh.read(min(_READ_BUFFER_SIZE, target - pos))
                if not data:
                    break
                pos += len(data)
                line_no += data.count(b"\n")
            result[target] = line_no

    for offset in offsets:
        result.setdefault(offset, line_no)
    return result


def _parse_log_chunk_worker(args: tuple[str, int, int, int]) -> list[LogRecord]:
    path, start, end, start_line_no = args
    records: list[LogRecord] = []
    current: LogRecord | None = None
    current_raw_parts: list[str] | None = None
    log_line_match = LOG_LINE_RE.match
    looks_like_log_record_start = _looks_like_log_record_start
    finalize = _finalize
    parse_timestamp_sec = _parse_timestamp_sec
    log_record = LogRecord
    timestamp_cache: dict[str, float | None] = {}
    line_no = start_line_no

    with open(path, "rb", buffering=_READ_BUFFER_SIZE) as fh:
        fh.seek(start)
        while fh.tell() < end:
            raw_line = fh.readline()
            if not raw_line:
                break
            line = raw_line.decode("utf-8", "replace").rstrip("\n")
            m = log_line_match(line) if looks_like_log_record_start(line) else None
            if m:
                if current is not None:
                    finalize(current, current_raw_parts)
                    records.append(current)
                ts_text, level, module, thread, _message = m.groups()
                current = log_record(
                    id=0,
                    file_path=path,
                    raw=line,
                    timestamp_sec=parse_timestamp_sec(ts_text, timestamp_cache),
                    timestamp_text=ts_text,
                    line_number=line_no,
                    level=level,
                    module=module,
                    thread=thread,
                )
                current_raw_parts = None
            elif current is not None:
                if current_raw_parts is None:
                    current_raw_parts = [current.raw]
                current_raw_parts.append(line)
            elif line.strip():
                records.append(
                    log_record(
                        id=0,
                        file_path=path,
                        raw=line,
                        line_number=line_no,
                        message_length=len(line),
                    )
                )
            line_no += 1

    if current is not None:
        finalize(current, current_raw_parts)
        records.append(current)
    return records


def _multiprocessing_context():
    try:
        return multiprocessing.get_context("fork")
    except ValueError:
        return None


def _looks_like_log_record_start(line: str) -> bool:
    # Cheap precheck before running the full regexp. Large ya logs may contain
    # millions of continuation lines, and regexp attempts on all of them are
    # measurable in the bundled ya Python runtime.
    #
    # A regular record starts with a fixed-width timestamp followed by a space:
    # "2026-05-18 12:34:56,789 INFO (module.name) [thread] message"
    # We only check timestamp separators here:
    # "YYYY-MM-DD HH:MM:SS,mmm "
    return (
        len(line) > 23
        and line[4] == "-"
        and line[7] == "-"
        and line[10] == " "
        and line[13] == ":"
        and line[16] == ":"
        and line[19] == ","
        and line[23] == " "
    )


def _looks_like_log_record_start_bytes(line: bytes) -> bool:
    # Same as _looks_like_log_record_start(), but for bytes:
    # 45 == "-", 32 == " ", 58 == ":", 44 == ",".
    return (
        len(line) > 23
        and line[4] == 45
        and line[7] == 45
        and line[10] == 32
        and line[13] == 58
        and line[16] == 58
        and line[19] == 44
        and line[23] == 32
    )


def _parse_timestamp_sec(ts_text: str, cache: dict[str, float | None]) -> float | None:
    # Fast path for "YYYY-MM-DD HH:MM:SS,mmm".
    #
    # datetime.strptime(...).timestamp() is very expensive on large logs: it can
    # dominate total parse time. ya logs usually contain many records per second,
    # so cache epoch seconds by "YYYY-MM-DD HH:MM:SS" and only parse milliseconds
    # per record.
    try:
        sec_text = ts_text[:19]
        if sec_text in cache:
            base_sec = cache[sec_text]
        else:
            year = int(ts_text[0:4])
            month = int(ts_text[5:7])
            day = int(ts_text[8:10])
            hour = int(ts_text[11:13])
            minute = int(ts_text[14:16])
            second = int(ts_text[17:19])
            if not _is_valid_datetime(year, month, day, hour, minute, second):
                cache[sec_text] = None
                return None
            base_sec = time.mktime((year, month, day, hour, minute, second, 0, 0, -1))
            cache[sec_text] = base_sec
        if base_sec is None:
            return None
        return base_sec + int(ts_text[20:23]) / 1000.0
    except (ValueError, OverflowError, OSError):
        return None


def _is_valid_datetime(year: int, month: int, day: int, hour: int, minute: int, second: int) -> bool:
    try:
        datetime.datetime(year, month, day, hour, minute, second)
        return True
    except ValueError:
        return False
