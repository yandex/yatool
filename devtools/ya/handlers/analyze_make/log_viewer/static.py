from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import Response
from library.python.resource import find as resource_find


def mount_static_routes(app: FastAPI) -> None:
    @app.get("/")
    def index() -> Response:
        return _serve_static("index.html", "text/html; charset=utf-8")

    @app.get("/assets/{path:path}")
    def assets(path: str) -> Response:
        if path.endswith(".js"):
            media_type = "application/javascript; charset=utf-8"
        elif path.endswith(".css"):
            media_type = "text/css; charset=utf-8"
        else:
            media_type = "application/octet-stream"
        return _serve_static(f"assets/{path}", media_type)


def static_assets_available() -> bool:
    return _find_static_resource("index.html") is not None


def _serve_static(name: str, media_type: str) -> Response:
    body = _find_static_resource(name)
    if body is None:
        body = _find_dev_static_file(name)
    if body is None:
        raise HTTPException(status_code=404, detail=f"Static asset {name} not found")

    return Response(
        content=body,
        media_type=media_type,
        headers={"Cache-Control": "no-cache"},
    )


def _find_static_resource(name: str) -> bytes | None:
    for key in (
        f"/log_viewer/static/{name}",
        f"log_viewer/static/{name}",
        f"/resfs/file/log_viewer/static/{name}",
        f"resfs/file/log_viewer/static/{name}",
    ):
        body = resource_find(key)
        if body is not None:
            return body
    return None


def _find_dev_static_file(name: str) -> bytes | None:
    candidates = [
        Path(__file__).resolve().parent / "static" / name,
        Path.cwd() / "static" / name,
    ]
    for path in candidates:
        try:
            with path.open("rb") as fh:
                return fh.read()
        except FileNotFoundError:
            continue
    return None
