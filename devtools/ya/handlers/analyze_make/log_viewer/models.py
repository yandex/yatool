from dataclasses import dataclass


@dataclass(slots=True)
class LogRecord:
    id: int
    file_path: str
    raw: str
    timestamp_sec: float | None = None
    timestamp_text: str | None = None
    line_number: int | None = None
    level: str | None = None
    module: str | None = None
    thread: str | None = None
    message_length: int = 0


@dataclass(slots=True)
class LogStage:
    id: int
    tag: str
    group: str | None
    start_record_id: int
    end_record_id: int
    start_time_sec: float | None = None
    finish_time_sec: float | None = None
    start_line_number: int | None = None
    end_line_number: int | None = None
    start_timestamp_text: str | None = None
    end_timestamp_text: str | None = None
    record_count: int = 0
