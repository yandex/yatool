import os
from pathlib import Path
from typing import Mapping, Sequence

import devtools.ya.core.yarg as yarg
import devtools.ya.yalibrary.app_ctx

DEFAULT_PORT = 8765
_HOST = "127.0.0.1"


class PrettyLogError(Exception):
    pass


class PrettyLogOptions(yarg.Options):
    def __init__(self) -> None:
        super().__init__()
        self.pretty_log_logs: list[str] = []
        self.pretty_log_port = DEFAULT_PORT

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--log"],
                help="ya .log file to open; repeatable. Defaults to the latest previous ya log.",
                hook=yarg.SetAppendHook("pretty_log_logs"),
            ),
            yarg.ArgConsumer(
                ["--port"],
                help="HTTP port for local UI",
                hook=yarg.SetValueHook("pretty_log_port", transform=int),
            ),
        ]

    def postprocess(self):
        if not (1 <= self.pretty_log_port <= 65535):
            raise yarg.ArgsValidatingException("--port must be between 1 and 65535")


def run(params) -> int:
    current_log_file = _current_ya_log_file()

    try:
        log_files = resolve_log_files(params.pretty_log_logs, current_log_file=current_log_file)
    except PrettyLogError as e:
        raise yarg.ArgsValidatingException(str(e))

    log_viewer_cli = _get_log_viewer_cli()
    try:
        return log_viewer_cli.run_server(
            log_files,
            host=_HOST,
            port=params.pretty_log_port,
            db_path=None,
            rebuild_db=False,
            log_workers=1,
        )
    except log_viewer_cli.LogViewerError as e:
        raise yarg.ArgsValidatingException(str(e))


def resolve_log_files(
    explicit_logs: Sequence[str],
    *,
    current_log_file: str | None = None,
    logs_root: str | Path | None = None,
) -> list[str]:
    if explicit_logs:
        return validate_explicit_log_files(explicit_logs)

    return [find_latest_log_file(logs_root=logs_root, current_log_file=current_log_file)]


def validate_explicit_log_files(paths: Sequence[str]) -> list[str]:
    log_viewer_cli = _get_log_viewer_cli()
    try:
        return log_viewer_cli.validate_input_files(paths)
    except log_viewer_cli.LogViewerError as e:
        raise PrettyLogError(str(e))


def find_latest_log_file(
    *,
    logs_root: str | Path | None = None,
    current_log_file: str | None = None,
) -> str:
    root = Path(logs_root).expanduser() if logs_root is not None else default_logs_root()
    current = _normalize_path(current_log_file) if current_log_file else None

    latest: tuple[tuple[int, str], str] | None = None
    try:
        candidates = root.rglob("*.log") if root.exists() else ()
        for path in candidates:
            try:
                if not path.is_file():
                    continue
                normalized = _normalize_path(path)
                if current is not None and normalized == current:
                    continue
                stat = path.stat()
            except OSError:
                continue

            key = (stat.st_mtime_ns, normalized)
            if latest is None or key > latest[0]:
                latest = (key, normalized)
    except OSError:
        latest = None

    if latest is None:
        raise PrettyLogError(_no_logs_message(root, current_log_file))

    return latest[1]


def default_logs_root(env: Mapping[str, str] | None = None) -> Path:
    env = os.environ if env is None else env

    logs_root = env.get("YA_LOGS_ROOT")
    if logs_root:
        return Path(logs_root).expanduser()

    cache_dir = env.get("YA_CACHE_DIR")
    if cache_dir:
        return Path(cache_dir).expanduser() / "logs"

    return Path.home() / ".ya" / "logs"


def _no_logs_message(root: Path, current_log_file: str | None) -> str:
    if current_log_file:
        return (
            f"No previous ya .log files found under {root} "
            "(the current pretty-log log is ignored). Specify --log PATH."
        )
    return f"No ya .log files found under {root}. Specify --log PATH (or run ya once first)."


def _normalize_path(path: str | Path) -> str:
    return str(Path(path).expanduser().resolve(strict=False))


def _current_ya_log_file() -> str | None:
    ctx = devtools.ya.yalibrary.app_ctx.get_app_ctx()
    return getattr(ctx, "file_log", None)


def _get_log_viewer_cli():
    from devtools.ya.handlers.analyze_make.log_viewer import cli as log_viewer_cli

    return log_viewer_cli
