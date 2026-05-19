from dataclasses import dataclass
import re
import sqlite3
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Iterator, Sequence

from devtools.ya.handlers.analyze_make.log_viewer.models import LogRecord, LogStage

_FTS_TOKEN_RE = re.compile(r"[^\W_]+", re.UNICODE)
_CACHE_FORMAT_VERSION = "8"
_STAGE_TRACER_RE = re.compile(r"\b(Start|Finish) stage tag=([^,]+), group=([^,]+), time=([0-9.]+)")
_SET_STAGE_RE = re.compile(r"\bSet stage ([^\s=]+)_(started|finished)=([0-9.]+)")
_PROFILE_STAGE_RE = re.compile(r"\bProfile step ([^\s]+)_(started|finished)\s+-\s+([-+0-9.eE]+)")
_STAGE_TRACER_MARKER = 1
_SET_STAGE_MARKER = 2
_PROFILE_STAGE_MARKER = 4


@dataclass(slots=True)
class StoreIngestTimings:
    sqlite_insert_sec: float = 0.0


class LogRepository:
    """SQLite-backed storage for normalized log records"""

    _BATCH_SIZE = 2000

    def __init__(self, db_path: str | None = None, *, reset: bool = True) -> None:
        if db_path is None:
            tmp_dir = Path(tempfile.mkdtemp(prefix="ya-log-viewer-"))
            db_path = str(tmp_dir / "logs.sqlite")
            self._owns_temp_dir = tmp_dir
        else:
            self._owns_temp_dir = None

        self.db_path = str(Path(db_path).expanduser())
        Path(self.db_path).parent.mkdir(parents=True, exist_ok=True)
        if reset:
            self._remove_existing_db_files(self.db_path)

        self._lock = threading.RLock()
        self._conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._closed = False
        self._next_id = 0

        with self._lock:
            self._configure_connection()
            self._create_schema()
            self._next_id = self._read_next_id()

    def ingest(
        self,
        file_path: str,
        parser_iter: Iterator[LogRecord],
        *,
        timings: StoreIngestTimings | None = None,
    ) -> int:
        added = 0
        batch: list[tuple[Any, ...]] = []

        with self._lock, self._conn:
            self._set_metadata("ingest_complete", "0")
            path, size_bytes, mtime_ns = log_file_identity(file_path)
            file_id = self._create_file(path, size_bytes, mtime_ns)
            stage_builder = _StageIndexBuilder(file_id)

            for rec in parser_iter:
                rec.id = self._next_id
                self._next_id += 1
                batch.append(_record_to_row(rec, file_id))
                stage_builder.add_record(rec)
                added += 1

                if len(batch) >= self._BATCH_SIZE:
                    self._insert_log_records_batch(batch, timings)
                    batch.clear()

            if batch:
                self._insert_log_records_batch(batch, timings)

            stage_rows = stage_builder.finish()
            if stage_rows:
                self._insert_stages(stage_rows)

            self._conn.execute("UPDATE files SET records = ? WHERE id = ?", (added, file_id))

        return added

    def _insert_log_records_batch(
        self,
        batch: list[tuple[Any, ...]],
        timings: StoreIngestTimings | None,
    ) -> None:
        if timings is None:
            self._insert_batch(batch)
            return

        started = time.perf_counter()
        try:
            self._insert_batch(batch)
        finally:
            timings.sqlite_insert_sec += time.perf_counter() - started

    def can_reuse(self, file_paths: Sequence[str]) -> bool:
        """Return True if this DB already contains a complete parse of file_paths.

        The check intentionally uses cheap filesystem metadata (canonical path,
        file size, mtime_ns) instead of hashing the whole log file: on multi-GB
        logs a full hash is already a noticeable part of the work we are trying
        to avoid.
        """
        expected = [log_file_identity(path) for path in file_paths]
        if not expected or any(size is None or mtime is None for _, size, mtime in expected):
            return False

        with self._lock:
            if self._get_metadata("cache_format_version") != _CACHE_FORMAT_VERSION:
                return False
            if self._get_metadata("ingest_complete") != "1":
                return False

            rows = self._conn.execute("SELECT path, size_bytes, mtime_ns, records FROM files ORDER BY id").fetchall()
            if len(rows) != len(expected):
                return False

            for row, (path, size_bytes, mtime_ns) in zip(rows, expected):
                if row["path"] != path or row["size_bytes"] != size_bytes or row["mtime_ns"] != mtime_ns:
                    return False

            records_in_files = sum(int(row["records"]) for row in rows)
            return records_in_files == self._scalar_int("SELECT COUNT(*) FROM log_records")

    def reset_storage(self) -> None:
        with self._lock, self._conn:
            self._conn.executescript("""
                DROP TABLE IF EXISTS log_records_fts;
                DROP TABLE IF EXISTS stages;
                DROP TABLE IF EXISTS log_records;
                DROP TABLE IF EXISTS files;
                DROP TABLE IF EXISTS metadata;
                """)
            self._create_schema()
            self._next_id = 0

    def finalize(self) -> None:
        """Create query indexes after bulk ingest.

        Creating B-tree indexes once after the initial load is faster for large
        logs than maintaining them during every insert. The FTS index is also
        rebuilt here from the content table so bulk ingest does not maintain it
        row-by-row.
        """
        with self._lock, self._conn:
            self._create_indexes()
            self._analyze_query_planner()
            self._rebuild_fts_index()
            self._set_metadata("cache_format_version", _CACHE_FORMAT_VERSION)
            self._set_metadata("ingest_complete", "1")

    def query(
        self,
        *,
        levels: list[str] | None = None,
        modules: list[str] | None = None,
        threads: list[str] | None = None,
        level_op: str = "eq",
        module_op: str = "eq",
        thread_op: str = "eq",
        q: str | None = None,
        time_from_sec: float | None = None,
        time_to_sec: float | None = None,
        stage_id: int | None = None,
        offset: int = 0,
        limit: int = 100,
    ) -> tuple[list[LogRecord], int]:
        if stage_id is not None:
            stage_bounds = self._stage_bounds(stage_id)
            if stage_bounds is None:
                return [], 0
        else:
            stage_bounds = None

        where, params = self._build_where(
            levels=levels,
            modules=modules,
            threads=threads,
            level_op=level_op,
            module_op=module_op,
            thread_op=thread_op,
            time_from_sec=time_from_sec,
            time_to_sec=time_to_sec,
            table_alias="r",
        )
        if stage_bounds is not None:
            file_id, start_record_id, end_record_id = stage_bounds
            where.append("r.file_id = ?")
            params.append(file_id)
            where.append("r.id BETWEEN ? AND ?")
            params.extend([start_record_id, end_record_id])
        where_sql = f" WHERE {' AND '.join(where)}" if where else ""
        from_sql = "FROM log_records AS r"
        order_by_sql = "r.id"

        fts_query = _fts_query(q) if q else None
        if fts_query:
            # Force SQLite to evaluate FTS first. With regular JOIN the query
            # planner may choose a selective-looking B-tree filter (for example
            # module=...) as the outer loop and then probe the FTS virtual table
            # for every matching row. On large logs this can look like a hang.
            from_sql = "FROM log_records_fts CROSS JOIN log_records AS r ON r.id = log_records_fts.rowid"
            # r.id and log_records_fts.rowid are equal because of the join above,
            # but ordering by FTS rowid avoids a temp B-tree sort of all matches.
            order_by_sql = "log_records_fts.rowid"
            where.insert(0, "log_records_fts MATCH ?")
            params.insert(0, fts_query)
            where_sql = f" WHERE {' AND '.join(where)}"
        elif q:
            pattern = _like_pattern(q)
            where.append("r.raw LIKE ? ESCAPE '\\'")
            params.append(pattern)
            where_sql = f" WHERE {' AND '.join(where)}"

        with self._lock:
            total = int(self._conn.execute(f"SELECT COUNT(*) {from_sql}{where_sql}", params).fetchone()[0])
            rows = self._conn.execute(
                f"""
                SELECT r.*, f.path AS file_path
                {from_sql}
                JOIN files AS f ON f.id = r.file_id
                {where_sql}
                ORDER BY {order_by_sql}
                LIMIT ? OFFSET ?
                """,
                [*params, limit, offset],
            ).fetchall()

        return [_row_to_record(row) for row in rows], total

    def stages(self) -> list[LogStage]:
        with self._lock:
            rows = self._conn.execute("""
                SELECT
                    s.*,
                    start_rec.timestamp_text AS start_timestamp_text,
                    end_rec.timestamp_text AS end_timestamp_text
                FROM stages AS s
                LEFT JOIN log_records AS start_rec ON start_rec.id = s.start_record_id
                LEFT JOIN log_records AS end_rec ON end_rec.id = s.end_record_id
                WHERE s.has_start = 1 AND s.has_finish = 1
                ORDER BY COALESCE(s.start_time_sec, s.finish_time_sec), s.start_record_id, s.id
                """).fetchall()
        return [_row_to_stage(row) for row in rows]

    def get_stage(self, stage_id: int) -> LogStage | None:
        with self._lock:
            row = self._conn.execute(
                """
                SELECT
                    s.*,
                    start_rec.timestamp_text AS start_timestamp_text,
                    end_rec.timestamp_text AS end_timestamp_text
                FROM stages AS s
                LEFT JOIN log_records AS start_rec ON start_rec.id = s.start_record_id
                LEFT JOIN log_records AS end_rec ON end_rec.id = s.end_record_id
                WHERE s.id = ?
                  AND s.has_start = 1
                  AND s.has_finish = 1
                """,
                (stage_id,),
            ).fetchone()
        return _row_to_stage(row) if row is not None else None

    def stages_for_record(self, record_id: int) -> list[LogStage]:
        with self._lock:
            record_row = self._conn.execute("SELECT file_id FROM log_records WHERE id = ?", (record_id,)).fetchone()
            if record_row is None:
                return []

            rows = self._conn.execute(
                """
                SELECT
                    s.*,
                    start_rec.timestamp_text AS start_timestamp_text,
                    end_rec.timestamp_text AS end_timestamp_text
                FROM stages AS s
                LEFT JOIN log_records AS start_rec ON start_rec.id = s.start_record_id
                LEFT JOIN log_records AS end_rec ON end_rec.id = s.end_record_id
                WHERE s.file_id = ?
                  AND ? BETWEEN s.start_record_id AND s.end_record_id
                  AND s.has_start = 1
                  AND s.has_finish = 1
                ORDER BY s.start_record_id, s.end_record_id DESC, s.id
                LIMIT 100
                """,
                (record_row["file_id"], record_id),
            ).fetchall()
        return [_row_to_stage(row) for row in rows]

    def filter_values(self, field: str, *, q: str | None = None, limit: int = 100) -> list[tuple[str, int]]:
        if field not in {"level", "module", "thread"}:
            raise ValueError(f"Unsupported filter field: {field}")

        where = [f"{field} IS NOT NULL", f"{field} != ''"]
        params: list[Any] = []
        if q:
            where.append(f"{field} LIKE ? ESCAPE '\\'")
            params.append(_like_pattern(q))

        limit = max(1, min(limit, 500))
        with self._lock:
            rows = self._conn.execute(
                f"""
                SELECT {field} AS value, COUNT(*) AS count
                FROM log_records
                WHERE {' AND '.join(where)}
                GROUP BY {field}
                ORDER BY count DESC, value
                LIMIT ?
                """,
                [*params, limit],
            ).fetchall()
        return [(row["value"], row["count"]) for row in rows]

    def get(self, record_id: int) -> LogRecord | None:
        with self._lock:
            row = self._conn.execute(
                """
                SELECT r.*, f.path AS file_path
                FROM log_records AS r
                JOIN files AS f ON f.id = r.file_id
                WHERE r.id = ?
                """,
                (record_id,),
            ).fetchone()
        return _row_to_record(row) if row is not None else None

    def record_position(self, record_id: int) -> int | None:
        """Return zero-based record position in the unfiltered log order."""
        with self._lock:
            row = self._conn.execute("SELECT id FROM log_records WHERE id = ?", (record_id,)).fetchone()
            if row is None:
                return None
            # Record ids are assigned sequentially from zero and never deleted,
            # so id is already the zero-based position in full log order.
            return int(row["id"])

    def overview(self) -> dict:
        with self._lock:
            first_timestamp_text = self._first_log_timestamp_text()
            return {
                "total": self._scalar_int("SELECT COUNT(*) FROM log_records"),
                "files": [
                    {"id": row["id"], "path": row["path"], "records": row["records"]}
                    for row in self._conn.execute("SELECT id, path, records FROM files ORDER BY id")
                ],
                "by_level": self._count_by("level", skip_empty=True),
                "top_modules": self._top_values("module", 50),
                "top_threads": self._top_values("thread", 50),
                "stage_count": self._scalar_int("SELECT COUNT(*) FROM stages WHERE has_start = 1 AND has_finish = 1"),
                "log_date": (
                    first_timestamp_text[:10] if first_timestamp_text and len(first_timestamp_text) >= 10 else None
                ),
                "first_timestamp_text": first_timestamp_text,
                "last_timestamp_text": self._last_log_timestamp_text(),
                "db_path": self.db_path,
            }

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._conn.close()
            self._closed = True

    def _configure_connection(self) -> None:
        self._conn.execute("PRAGMA journal_mode = WAL")
        self._conn.execute("PRAGMA synchronous = NORMAL")
        self._conn.execute("PRAGMA temp_store = MEMORY")
        self._conn.execute("PRAGMA cache_size = -131072")
        self._conn.execute("PRAGMA mmap_size = 268435456")

    def _create_schema(self) -> None:
        self._conn.executescript("""
            CREATE TABLE IF NOT EXISTS files (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT NOT NULL,
                size_bytes INTEGER,
                mtime_ns INTEGER,
                records INTEGER NOT NULL DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS metadata (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS log_records (
                id INTEGER PRIMARY KEY,
                file_id INTEGER NOT NULL REFERENCES files(id),
                raw TEXT NOT NULL,
                timestamp_sec REAL,
                timestamp_text TEXT,
                line_number INTEGER,
                level TEXT,
                module TEXT,
                thread TEXT,
                message_length INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS stages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                file_id INTEGER NOT NULL REFERENCES files(id),
                tag TEXT NOT NULL,
                group_name TEXT,
                start_record_id INTEGER NOT NULL,
                end_record_id INTEGER NOT NULL,
                start_time_sec REAL,
                finish_time_sec REAL,
                start_line_number INTEGER,
                end_line_number INTEGER,
                has_start INTEGER NOT NULL,
                has_finish INTEGER NOT NULL,
                record_count INTEGER NOT NULL
            );

            CREATE VIRTUAL TABLE IF NOT EXISTS log_records_fts USING fts5(
                raw,
                content='log_records',
                content_rowid='id',
                columnsize=0
            );
            """)
        self._ensure_file_metadata_columns()
        self._conn.commit()

    def _create_indexes(self) -> None:
        self._conn.executescript("""
            CREATE INDEX IF NOT EXISTS log_records_timestamp_sec_id_idx ON log_records(timestamp_sec, id);
            CREATE INDEX IF NOT EXISTS log_records_level_id_idx ON log_records(level, id);
            CREATE INDEX IF NOT EXISTS log_records_module_id_idx ON log_records(module, id);
            CREATE INDEX IF NOT EXISTS log_records_thread_id_idx ON log_records(thread, id);
            CREATE INDEX IF NOT EXISTS stages_record_range_idx ON stages(file_id, start_record_id, end_record_id);
            """)

    def _analyze_query_planner(self) -> None:
        # Without statistics SQLite may choose timestamp_sec index scans for the
        # default "whole log" time range and then sort almost the entire log by
        # id. ANALYZE gives the planner enough cardinality data to choose rowid
        # scans for broad ranges and timestamp index scans for narrow ranges.
        self._conn.execute("ANALYZE log_records")
        self._conn.execute("ANALYZE stages")

    def _rebuild_fts_index(self) -> None:
        self._conn.execute("INSERT INTO log_records_fts(log_records_fts) VALUES ('rebuild')")

    def _create_file(self, file_path: str, size_bytes: int | None, mtime_ns: int | None) -> int:
        cursor = self._conn.execute(
            "INSERT INTO files(path, size_bytes, mtime_ns) VALUES (?, ?, ?)",
            (file_path, size_bytes, mtime_ns),
        )
        return int(cursor.lastrowid)

    def _read_next_id(self) -> int:
        return self._scalar_int("SELECT COALESCE(MAX(id) + 1, 0) FROM log_records")

    def _insert_batch(self, batch: list[tuple[Any, ...]]) -> None:
        self._conn.executemany(
            """
            INSERT INTO log_records(
                id,
                file_id,
                raw,
                timestamp_sec,
                timestamp_text,
                line_number,
                level,
                module,
                thread,
                message_length
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            batch,
        )

    def _insert_stages(self, batch: list[tuple[Any, ...]]) -> None:
        self._conn.executemany(
            """
            INSERT INTO stages(
                file_id,
                tag,
                group_name,
                start_record_id,
                end_record_id,
                start_time_sec,
                finish_time_sec,
                start_line_number,
                end_line_number,
                has_start,
                has_finish,
                record_count
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            batch,
        )

    def _stage_bounds(self, stage_id: int) -> tuple[int, int, int] | None:
        with self._lock:
            row = self._conn.execute(
                """
                SELECT file_id, start_record_id, end_record_id
                FROM stages
                WHERE id = ? AND has_start = 1 AND has_finish = 1
                """,
                (stage_id,),
            ).fetchone()
        if row is None:
            return None
        return row["file_id"], row["start_record_id"], row["end_record_id"]

    def _build_where(
        self,
        *,
        levels: list[str] | None,
        modules: list[str] | None,
        threads: list[str] | None,
        level_op: str,
        module_op: str,
        thread_op: str,
        time_from_sec: float | None,
        time_to_sec: float | None,
        table_alias: str | None = None,
    ) -> tuple[list[str], list[Any]]:
        where: list[str] = []
        params: list[Any] = []

        _append_nullable_filter(where, params, _column("level", table_alias), levels, level_op)
        _append_nullable_filter(where, params, _column("module", table_alias), modules, module_op)
        _append_nullable_filter(where, params, _column("thread", table_alias), threads, thread_op)

        if time_from_sec is not None:
            where.append(f"{_column('timestamp_sec', table_alias)} >= ?")
            params.append(time_from_sec)

        if time_to_sec is not None:
            where.append(f"{_column('timestamp_sec', table_alias)} <= ?")
            params.append(time_to_sec)

        return where, params

    def _count_by(self, column: str, *, skip_empty: bool = False) -> dict[str, int]:
        where = f"WHERE {column} IS NOT NULL AND {column} != ''" if skip_empty else ""
        return {row["value"]: row["count"] for row in self._conn.execute(f"""
                SELECT {column} AS value, COUNT(*) AS count
                FROM log_records
                {where}
                GROUP BY {column}
                ORDER BY count DESC, value
                """)}

    def _top_values(self, column: str, limit: int) -> list[tuple[str, int]]:
        return [
            (row["value"], row["count"])
            for row in self._conn.execute(
                f"""
                SELECT {column} AS value, COUNT(*) AS count
                FROM log_records
                WHERE {column} IS NOT NULL AND {column} != ''
                GROUP BY {column}
                ORDER BY count DESC, value
                LIMIT ?
                """,
                (limit,),
            )
        ]

    def _scalar_int(self, sql: str, params: Sequence[Any] = ()) -> int:
        return int(self._conn.execute(sql, params).fetchone()[0])

    def _get_metadata(self, key: str) -> str | None:
        row = self._conn.execute("SELECT value FROM metadata WHERE key = ?", (key,)).fetchone()
        return None if row is None else str(row["value"])

    def _set_metadata(self, key: str, value: str) -> None:
        self._conn.execute(
            """
            INSERT OR REPLACE INTO metadata(key, value) VALUES (?, ?)
            """,
            (key, value),
        )

    def _ensure_file_metadata_columns(self) -> None:
        columns = {row["name"] for row in self._conn.execute("PRAGMA table_info(files)")}
        if "size_bytes" not in columns:
            self._conn.execute("ALTER TABLE files ADD COLUMN size_bytes INTEGER")
        if "mtime_ns" not in columns:
            self._conn.execute("ALTER TABLE files ADD COLUMN mtime_ns INTEGER")

    def _first_log_timestamp_text(self) -> str | None:
        row = self._conn.execute("""
            SELECT timestamp_text
            FROM log_records
            WHERE timestamp_text IS NOT NULL AND timestamp_text != ''
              AND timestamp_sec IS NOT NULL
            ORDER BY timestamp_sec, id
            LIMIT 1
            """).fetchone()
        if row is not None:
            return row["timestamp_text"]

        row = self._conn.execute("""
            SELECT timestamp_text
            FROM log_records
            WHERE timestamp_text IS NOT NULL AND timestamp_text != ''
            ORDER BY id
            LIMIT 1
            """).fetchone()
        return None if row is None else row["timestamp_text"]

    def _last_log_timestamp_text(self) -> str | None:
        row = self._conn.execute("""
            SELECT timestamp_text
            FROM log_records
            WHERE timestamp_text IS NOT NULL AND timestamp_text != ''
              AND timestamp_sec IS NOT NULL
            ORDER BY timestamp_sec DESC, id DESC
            LIMIT 1
            """).fetchone()
        if row is not None:
            return row["timestamp_text"]

        row = self._conn.execute("""
            SELECT timestamp_text
            FROM log_records
            WHERE timestamp_text IS NOT NULL AND timestamp_text != ''
            ORDER BY id DESC
            LIMIT 1
            """).fetchone()
        return None if row is None else row["timestamp_text"]

    @staticmethod
    def _remove_existing_db_files(db_path: str) -> None:
        for suffix in ("", "-wal", "-shm"):
            try:
                Path(db_path + suffix).unlink()
            except FileNotFoundError:
                pass


def log_file_identity(file_path: str) -> tuple[str, int | None, int | None]:
    path = Path(file_path).expanduser()
    try:
        stat = path.stat()
        return str(path.resolve()), int(stat.st_size), int(stat.st_mtime_ns)
    except OSError:
        return str(path), None, None


class _StageEvent:
    __slots__ = ("kind", "tag", "group", "event_time", "record_id", "line_number", "pos")

    def __init__(
        self,
        *,
        kind: str,
        tag: str,
        group: str | None,
        event_time: float | None,
        record_id: int,
        line_number: int | None,
        pos: int,
    ) -> None:
        self.kind = kind
        self.tag = tag
        self.group = group
        self.event_time = event_time
        self.record_id = record_id
        self.line_number = line_number
        self.pos = pos


class _StageDraft:
    __slots__ = (
        "file_id",
        "tag",
        "group",
        "start_record_id",
        "end_record_id",
        "start_time_sec",
        "finish_time_sec",
        "start_line_number",
        "end_line_number",
        "has_start",
        "has_finish",
    )

    def __init__(self, *, file_id: int, tag: str, start_record_id: int) -> None:
        self.file_id = file_id
        self.tag = tag
        self.group: str | None = None
        self.start_record_id = start_record_id
        self.end_record_id = start_record_id
        self.start_time_sec: float | None = None
        self.finish_time_sec: float | None = None
        self.start_line_number: int | None = None
        self.end_line_number: int | None = None
        self.has_start = False
        self.has_finish = False

    def start(self, event: _StageEvent) -> None:
        self.has_start = True
        if event.group:
            self.group = event.group
        self.start_record_id = min(self.start_record_id, event.record_id)
        self.end_record_id = max(self.end_record_id, event.record_id)
        self.start_time_sec = _min_optional(self.start_time_sec, event.event_time)
        self.start_line_number = _min_optional_int(self.start_line_number, event.line_number)

    def finish(self, event: _StageEvent) -> None:
        self.has_finish = True
        if event.group and self.group is None:
            self.group = event.group
        self.end_record_id = max(self.end_record_id, event.record_id)
        self.finish_time_sec = _max_optional(self.finish_time_sec, event.event_time)
        self.end_line_number = _max_optional_int(self.end_line_number, event.line_number)

    def to_row(self) -> tuple[Any, ...]:
        return (
            self.file_id,
            self.tag,
            self.group,
            self.start_record_id,
            self.end_record_id,
            self.start_time_sec,
            self.finish_time_sec,
            self.start_line_number,
            self.end_line_number,
            int(self.has_start),
            int(self.has_finish),
            max(0, self.end_record_id - self.start_record_id + 1),
        )


class _StageIndexBuilder:
    def __init__(self, file_id: int) -> None:
        self._file_id = file_id
        self._first_record_id: int | None = None
        self._last_record_id: int | None = None
        self._active: dict[str, list[_StageDraft]] = {}
        self._completed: dict[str, list[_StageDraft]] = {}
        self._all: list[_StageDraft] = []

    def add_record(self, rec: LogRecord) -> None:
        if self._first_record_id is None:
            self._first_record_id = rec.id
        self._last_record_id = rec.id

        stage_event_markers = _stage_event_markers(rec.raw)
        if not stage_event_markers:
            return

        for event in _stage_events(rec, stage_event_markers):
            if event.kind == "start":
                self._on_start(event)
            else:
                self._on_finish(event)

    def finish(self) -> list[tuple[Any, ...]]:
        if self._last_record_id is not None:
            for drafts in self._active.values():
                for draft in drafts:
                    draft.end_record_id = max(draft.end_record_id, self._last_record_id)
            self._completed.update((tag, drafts) for tag, drafts in self._active.items() if drafts)
            self._active = {}

        return [
            draft.to_row()
            for draft in self._all
            if draft.has_start and draft.has_finish and draft.end_record_id >= draft.start_record_id
        ]

    def _on_start(self, event: _StageEvent) -> None:
        draft = self._find_active(event.tag, event.group)

        if draft is None:
            draft = self._new_draft(event.tag, event.record_id)
            self._active.setdefault(event.tag, []).append(draft)

        draft.start(event)

    def _on_finish(self, event: _StageEvent) -> None:
        draft = self._find_active(event.tag, event.group)
        if draft is None:
            draft = self._find_recent_completed(event.tag)

        if draft is None:
            draft = self._new_draft(
                event.tag, self._first_record_id if self._first_record_id is not None else event.record_id
            )

        draft.finish(event)
        self._complete(event.tag, draft)

    def _find_active(self, tag: str, group: str | None) -> _StageDraft | None:
        drafts = self._active.get(tag)
        if not drafts:
            return None
        if group:
            for draft in reversed(drafts):
                if draft.group in {None, group}:
                    return draft
        return drafts[-1]

    def _find_recent_completed(self, tag: str) -> _StageDraft | None:
        drafts = self._completed.get(tag)
        return drafts[-1] if drafts else None

    def _new_draft(self, tag: str, start_record_id: int) -> _StageDraft:
        draft = _StageDraft(file_id=self._file_id, tag=tag, start_record_id=start_record_id)
        self._all.append(draft)
        return draft

    def _complete(self, tag: str, draft: _StageDraft) -> None:
        active = self._active.get(tag)
        if active and draft in active:
            active.remove(draft)
            if not active:
                self._active.pop(tag, None)
        completed = self._completed.setdefault(tag, [])
        if not completed or completed[-1] is not draft:
            completed.append(draft)


def _stage_event_markers(raw: str) -> int:
    # Keep stage detection independent from parsed module names: if ya starts
    # logging the same stage messages from a different module, the viewer should
    # still index them. The cheap substring checks below prevent expensive regex
    # scans over every raw record.
    markers = 0
    if "stage" in raw:
        if " stage tag=" in raw:
            markers |= _STAGE_TRACER_MARKER
        if "Set stage " in raw:
            markers |= _SET_STAGE_MARKER
    if "Profile step " in raw:
        markers |= _PROFILE_STAGE_MARKER
    return markers


def _stage_events(rec: LogRecord, markers: int) -> list[_StageEvent]:
    events: list[_StageEvent] = []
    raw = rec.raw

    if markers & _STAGE_TRACER_MARKER:
        for match in _STAGE_TRACER_RE.finditer(raw):
            action, tag, group, event_time = match.groups()
            events.append(
                _StageEvent(
                    kind="start" if action == "Start" else "finish",
                    tag=tag,
                    group=group,
                    event_time=_float_or_none(event_time),
                    record_id=rec.id,
                    line_number=rec.line_number,
                    pos=match.start(),
                )
            )

    if markers & _SET_STAGE_MARKER:
        for match in _SET_STAGE_RE.finditer(raw):
            tag, action, event_time = match.groups()
            events.append(
                _StageEvent(
                    kind="start" if action == "started" else "finish",
                    tag=tag,
                    group=None,
                    event_time=_float_or_none(event_time),
                    record_id=rec.id,
                    line_number=rec.line_number,
                    pos=match.start(),
                )
            )

    if markers & _PROFILE_STAGE_MARKER:
        for match in _PROFILE_STAGE_RE.finditer(raw):
            tag, action, _ = match.groups()
            events.append(
                _StageEvent(
                    kind="start" if action == "started" else "finish",
                    tag=tag,
                    group=None,
                    event_time=rec.timestamp_sec,
                    record_id=rec.id,
                    line_number=rec.line_number,
                    pos=match.start(),
                )
            )

    events.sort(key=lambda event: event.pos)
    return events


def _float_or_none(value: str) -> float | None:
    try:
        return float(value)
    except ValueError:
        return None


def _min_optional(left: float | None, right: float | None) -> float | None:
    if left is None:
        return right
    if right is None:
        return left
    return min(left, right)


def _max_optional(left: float | None, right: float | None) -> float | None:
    if left is None:
        return right
    if right is None:
        return left
    return max(left, right)


def _min_optional_int(left: int | None, right: int | None) -> int | None:
    if left is None:
        return right
    if right is None:
        return left
    return min(left, right)


def _max_optional_int(left: int | None, right: int | None) -> int | None:
    if left is None:
        return right
    if right is None:
        return left
    return max(left, right)


def _record_to_row(rec: LogRecord, file_id: int) -> tuple[Any, ...]:
    message_length = rec.message_length or len(rec.raw)
    return (
        rec.id,
        file_id,
        rec.raw,
        rec.timestamp_sec,
        rec.timestamp_text,
        rec.line_number,
        rec.level,
        rec.module,
        rec.thread,
        message_length,
    )


def _row_to_record(row: sqlite3.Row) -> LogRecord:
    return LogRecord(
        id=row["id"],
        file_path=row["file_path"],
        raw=row["raw"],
        timestamp_sec=row["timestamp_sec"],
        timestamp_text=row["timestamp_text"],
        line_number=row["line_number"],
        level=row["level"],
        module=row["module"],
        thread=row["thread"],
        message_length=row["message_length"],
    )


def _row_to_stage(row: sqlite3.Row) -> LogStage:
    return LogStage(
        id=row["id"],
        tag=row["tag"],
        group=row["group_name"],
        start_record_id=row["start_record_id"],
        end_record_id=row["end_record_id"],
        start_time_sec=row["start_time_sec"],
        finish_time_sec=row["finish_time_sec"],
        start_line_number=row["start_line_number"],
        end_line_number=row["end_line_number"],
        start_timestamp_text=row["start_timestamp_text"],
        end_timestamp_text=row["end_timestamp_text"],
        record_count=row["record_count"],
    )


def _append_nullable_in(where: list[str], params: list[Any], column: str, values: list[str] | None) -> None:
    if not values:
        return

    non_empty = [v for v in values if v != ""]
    include_empty = len(non_empty) != len(values)

    clauses: list[str] = []
    if non_empty:
        placeholders = ", ".join("?" for _ in non_empty)
        clauses.append(f"{column} IN ({placeholders})")
        params.extend(non_empty)
    if include_empty:
        clauses.append(f"{column} IS NULL OR {column} = ''")

    where.append(f"({' OR '.join(clauses)})")


def _append_nullable_filter(
    where: list[str],
    params: list[Any],
    column: str,
    values: list[str] | None,
    op: str,
) -> None:
    if not values:
        return

    if op == "eq":
        _append_nullable_in(where, params, column, values)
        return
    if op != "ne":
        raise ValueError(f"Unsupported filter operator: {op}")

    non_empty = [v for v in values if v != ""]
    include_empty = len(non_empty) != len(values)

    clauses: list[str] = []
    if include_empty:
        clauses.append(f"{column} IS NOT NULL AND {column} != ''")
    elif non_empty:
        clauses.append(f"({column} IS NULL OR {column} = '' OR {column} NOT IN ({_placeholders(non_empty)}))")
        params.extend(non_empty)

    if include_empty and non_empty:
        clauses.append(f"{column} NOT IN ({_placeholders(non_empty)})")
        params.extend(non_empty)

    if clauses:
        where.append(f"({' AND '.join(clauses)})")


def _like_pattern(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
    return f"%{escaped}%"


def _fts_query(text: str | None) -> str | None:
    if not text:
        return None

    terms = _FTS_TOKEN_RE.findall(text)
    if not terms:
        return None
    return " + ".join(f'"{term}"*' for term in terms)


def _column(name: str, table_alias: str | None) -> str:
    return f"{table_alias}.{name}" if table_alias else name


def _placeholders(values: Sequence[Any]) -> str:
    return ", ".join("?" for _ in values)
