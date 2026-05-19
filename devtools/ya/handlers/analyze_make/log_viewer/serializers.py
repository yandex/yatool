from devtools.ya.handlers.analyze_make.log_viewer.models import LogRecord, LogStage

PREVIEW_LIMIT = 200


def to_log_item(rec: LogRecord) -> dict:
    preview_text = _preview_text(rec)
    item = _record_common(rec)
    item["preview"] = preview_text[:PREVIEW_LIMIT]
    item["preview_truncated"] = len(preview_text) > PREVIEW_LIMIT
    return item


def to_log_details(rec: LogRecord, *, stages: list[LogStage] | None = None) -> dict:
    item = _record_common(rec)
    item["raw"] = rec.raw
    if stages is not None:
        item["stages"] = [to_stage_item(stage) for stage in stages]
    return item


def to_stage_item(stage: LogStage) -> dict:
    return {
        "id": stage.id,
        "tag": stage.tag,
        "group": stage.group,
        "start_record_id": stage.start_record_id,
        "end_record_id": stage.end_record_id,
        "start_time_sec": stage.start_time_sec,
        "finish_time_sec": stage.finish_time_sec,
        "start_line_number": stage.start_line_number,
        "end_line_number": stage.end_line_number,
        "start_timestamp_text": stage.start_timestamp_text,
        "end_timestamp_text": stage.end_timestamp_text,
        "record_count": stage.record_count,
    }


def _record_common(rec: LogRecord) -> dict:
    return {
        "id": rec.id,
        "file_path": rec.file_path,
        "timestamp_sec": rec.timestamp_sec,
        "timestamp_text": rec.timestamp_text,
        "line_number": rec.line_number,
        "level": rec.level,
        "module": rec.module,
        "thread": rec.thread,
        "message_length": rec.message_length,
    }


def _preview_text(rec: LogRecord) -> str:
    """Return record text for the table preview without duplicated columns.

    The full raw record is still kept in SQLite/details, but list rows already
    show timestamp/level/module/thread in separate columns. For parsed .log
    records, strip exactly that prefix from the first line and keep the message
    plus continuation lines.
    """
    if rec.timestamp_text and rec.level is not None and rec.module is not None and rec.thread is not None:
        prefix = f"{rec.timestamp_text} {rec.level} ({rec.module}) [{rec.thread}] "
        if rec.raw.startswith(prefix):
            return rec.raw[len(prefix) :]

    return rec.raw
