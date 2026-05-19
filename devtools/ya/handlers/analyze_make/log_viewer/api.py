from datetime import datetime, timedelta
from typing import Literal

from fastapi import APIRouter, HTTPException, Query, Request

from devtools.ya.handlers.analyze_make.log_viewer.serializers import to_log_details, to_log_item, to_stage_item
from devtools.ya.handlers.analyze_make.log_viewer.store import LogRepository

router = APIRouter(prefix="/api")


def _store(request: Request) -> LogRepository:
    return request.app.state.store


@router.get("/logs/overview")
def get_logs_overview(request: Request) -> dict:
    """Return counters and metadata for the loaded log files."""
    return _store(request).overview()


@router.get("/logs")
def list_logs(
    request: Request,
    level: list[str] | None = Query(None),
    level_op: Literal["eq", "ne"] = Query("eq"),
    module: list[str] | None = Query(None),
    module_op: Literal["eq", "ne"] = Query("eq"),
    thread: list[str] | None = Query(None),
    thread_op: Literal["eq", "ne"] = Query("eq"),
    q: str | None = Query(None),
    time_from: str | None = Query(None),
    time_to: str | None = Query(None),
    stage_id: int | None = Query(None, ge=1),
    offset: int = Query(0, ge=0),
    limit: int = Query(100, ge=1, le=500),
) -> dict:
    """Return a page of log records matching text, time, field, and stage filters."""
    time_from_sec = _parse_time_bound(time_from, param_name="time_from", is_end=False)
    time_to_sec = _parse_time_bound(time_to, param_name="time_to", is_end=True)
    items, total = _store(request).query(
        levels=level,
        modules=module,
        threads=thread,
        level_op=level_op,
        module_op=module_op,
        thread_op=thread_op,
        q=q,
        time_from_sec=time_from_sec,
        time_to_sec=time_to_sec,
        stage_id=stage_id,
        offset=offset,
        limit=limit,
    )
    return {
        "total": total,
        "offset": offset,
        "limit": limit,
        "items": [to_log_item(rec) for rec in items],
    }


@router.get("/stages")
def list_stages(request: Request) -> dict:
    """Return all complete stage ranges found in the loaded logs."""
    return {"items": [to_stage_item(stage) for stage in _store(request).stages()]}


@router.get("/stages/{stage_id}")
def get_stage(request: Request, stage_id: int) -> dict:
    """Return one complete stage range by id."""
    stage = _store(request).get_stage(stage_id)
    if stage is None:
        raise HTTPException(status_code=404, detail="Stage not found")
    return to_stage_item(stage)


@router.get("/logs/filter-values")
def get_log_filter_values(
    request: Request,
    field: Literal["level", "module", "thread"] = Query(...),
    q: str | None = Query(None),
    limit: int = Query(100, ge=1, le=500),
) -> dict:
    """Return most frequent values for a log table filter field."""
    return {
        "field": field,
        "q": q or "",
        "items": _store(request).filter_values(field, q=q, limit=limit),
    }


@router.get("/logs/{record_id}")
def get_log(request: Request, record_id: int) -> dict:
    """Return full text and stage hierarchy for one log record."""
    store = _store(request)
    rec = store.get(record_id)
    if rec is None:
        raise HTTPException(status_code=404, detail="Record not found")
    return to_log_details(rec, stages=store.stages_for_record(record_id))


@router.get("/logs/{record_id}/position")
def get_log_position(request: Request, record_id: int, limit: int = Query(100, ge=1, le=500)) -> dict:
    """Return the page offset where a log record appears in the full log order."""
    position = _store(request).record_position(record_id)
    if position is None:
        raise HTTPException(status_code=404, detail="Record not found")

    return {
        "record_id": record_id,
        "position": position,
        "page_offset": (position // limit) * limit,
        "limit": limit,
    }


def _parse_time_bound(value: str | None, *, param_name: str, is_end: bool) -> float | None:
    if not value:
        return None

    text = value.strip()
    if not text:
        return None

    try:
        if len(text) == 10:
            dt = datetime.strptime(text, "%Y-%m-%d")
            if is_end:
                dt += timedelta(days=1)
                return dt.timestamp() - 0.001
            return dt.timestamp()

        return datetime.fromisoformat(text.replace(",", ".")).timestamp()
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid {param_name}: expected ISO date/time") from None
