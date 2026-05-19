from fastapi import FastAPI

from devtools.ya.handlers.analyze_make.log_viewer.api import router as api_router
from devtools.ya.handlers.analyze_make.log_viewer.static import mount_static_routes
from devtools.ya.handlers.analyze_make.log_viewer.store import LogRepository


def create_app(store: LogRepository) -> FastAPI:
    app = FastAPI(title="ya log_viewer", version="1")
    app.state.store = store
    app.include_router(api_router)
    mount_static_routes(app)
    return app
