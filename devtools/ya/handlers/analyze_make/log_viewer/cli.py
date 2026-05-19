import argparse
import hashlib
import os
from pathlib import Path
import resource
from typing import Sequence
import sys
import time

import uvicorn

from devtools.ya.handlers.analyze_make.log_viewer.app import create_app
from devtools.ya.handlers.analyze_make.log_viewer.ingest import (
    benchmark_log_file,
    format_benchmark_stats,
    format_ingest_stats,
    ingest_log_file,
)
from devtools.ya.handlers.analyze_make.log_viewer.static import static_assets_available
from devtools.ya.handlers.analyze_make.log_viewer.store import LogRepository, log_file_identity

_CACHE_TTL_DAYS = 14
_CACHE_FILE_PATTERNS = ("*.sqlite", "*.sqlite-wal", "*.sqlite-shm")


class LogViewerError(Exception):
    pass


def main(argv: Sequence[str] | None = None) -> int:
    parser = _make_parser()
    args = parser.parse_args(argv)

    try:
        if args.log_workers < 1:
            raise LogViewerError("--log-workers must be >= 1")

        if args.benchmark_log:
            benchmark_path = validate_input_files([args.benchmark_log], option_name="--benchmark-log")[0]
            stats = benchmark_log_file(benchmark_path, log_workers=args.log_workers)
            print(format_benchmark_stats(stats, log_workers=args.log_workers))
            return 0

        return run_server(
            args.log,
            host=args.host,
            port=args.port,
            db_path=args.db_path,
            rebuild_db=args.rebuild_db,
            log_workers=args.log_workers,
        )
    except LogViewerError as e:
        parser.error(str(e))


def run_server(
    log_files: Sequence[str],
    *,
    host: str = "127.0.0.1",
    port: int = 8765,
    db_path: str | None = None,
    rebuild_db: bool = False,
    log_workers: int = 1,
) -> int:
    if log_workers < 1:
        raise LogViewerError("--log-workers must be >= 1")
    if not log_files:
        raise LogViewerError("at least one --log is required")

    log_files = validate_input_files(log_files)
    if not db_path:
        db_path = _default_cache_db_path(log_files)
        _cleanup_cache_dir(Path(db_path).parent, exclude_db_path=db_path)

    store = LogRepository(db_path, reset=rebuild_db)
    try:
        if not rebuild_db and store.can_reuse(log_files):
            _touch_cache_files(store.db_path)
            print(f"Loading {len(log_files)} file(s)...")
            print(f"SQLite: {store.db_path} (cache hit, parsed logs reused)")
        else:
            store.reset_storage()
            print(f"Loading {len(log_files)} file(s)...")
            for path in log_files:
                print(format_ingest_stats(ingest_log_file(store, path, log_workers=log_workers)))

            started = time.perf_counter()
            store.finalize()
            print(f"SQLite: {store.db_path} (indexes built in {time.perf_counter() - started:.2f}s)")
        print(f"Memory: ~{_resident_mb():.0f} MB resident")
        print(f"Static assets: {'OK' if static_assets_available() else 'MISSING'}")

        app = create_app(store)
        print(f"Ready: http://{host}:{port}/  (API docs at /docs)")
        uvicorn.run(app, host=host, port=port, log_level="warning")
        return 0
    finally:
        store.close()


def _make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="log_viewer")
    parser.add_argument("--log", action="append", default=[], help="ya .log file (repeatable)")
    parser.add_argument("--benchmark-log", help="parse a ya .log file, print throughput, and exit")
    parser.add_argument(
        "--log-workers", type=int, default=1, help="parallel parser workers for .log files (default: 1)"
    )
    parser.add_argument(
        "--db-path", help="SQLite database path (default: persistent cache in $YA_CACHE_DIR/log_viewer)"
    )
    parser.add_argument(
        "--rebuild-db",
        action="store_true",
        help="ignore an existing matching SQLite cache and rebuild parsed logs",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    return parser


def validate_input_files(paths: Sequence[str], *, option_name: str = "--log") -> list[str]:
    validated: list[str] = []
    errors: list[str] = []
    seen: set[str] = set()

    for path in paths:
        p = Path(path).expanduser()
        if not p.exists():
            errors.append(f"{option_name} {path}: no such file")
            continue
        if not p.is_file():
            errors.append(f"{option_name} {path}: not a file")
            continue
        normalized = str(p.resolve())
        if normalized in seen:
            continue
        seen.add(normalized)
        validated.append(normalized)

    if errors:
        raise LogViewerError("input file error(s):\n  " + "\n  ".join(errors))

    return validated


def _validate_input_files(paths: list[str], parser: argparse.ArgumentParser) -> list[str]:
    try:
        return validate_input_files(paths)
    except LogViewerError as e:
        parser.error(str(e))


def _default_cache_db_path(log_files: Sequence[str]) -> str:
    digest = hashlib.sha256()
    for path in log_files:
        normalized_path, size_bytes, mtime_ns = log_file_identity(path)
        digest.update(normalized_path.encode("utf-8", "surrogateescape"))
        digest.update(b"\0")
        digest.update(str(size_bytes).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(mtime_ns).encode("ascii"))
        digest.update(b"\0")

    cache_root = Path(os.environ.get("YA_CACHE_DIR") or (Path.home() / ".ya"))
    return str(cache_root / "log_viewer" / f"{digest.hexdigest()[:32]}.sqlite")


def _cleanup_cache_dir(
    cache_dir: Path,
    *,
    ttl_days: int = _CACHE_TTL_DAYS,
    exclude_db_path: str | None = None,
    now: float | None = None,
) -> int:
    if ttl_days <= 0 or not cache_dir.exists():
        return 0

    cutoff = (time.time() if now is None else now) - ttl_days * 24 * 60 * 60
    excluded = set(_cache_files_for_db(exclude_db_path)) if exclude_db_path else set()
    removed = 0

    for pattern in _CACHE_FILE_PATTERNS:
        for path in cache_dir.glob(pattern):
            if path in excluded or not path.is_file():
                continue
            try:
                if path.stat().st_mtime < cutoff:
                    path.unlink()
                    removed += 1
            except OSError:
                continue

    return removed


def _touch_cache_files(db_path: str) -> None:
    now = time.time()
    for path in _cache_files_for_db(db_path):
        try:
            os.utime(path, (now, now))
        except FileNotFoundError:
            continue
        except OSError:
            continue


def _cache_files_for_db(db_path: str) -> tuple[Path, Path, Path]:
    return (Path(db_path), Path(db_path + "-wal"), Path(db_path + "-shm"))


def _resident_mb() -> float:
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    # macOS отдаёт ru_maxrss в байтах, Linux — в килобайтах
    if sys.platform == "darwin":
        return rss / (1024 * 1024)
    return rss / 1024
