PY3_PROGRAM(log_viewer)

PY_SRCS(
    __init__.py
    __main__.py
    api.py
    app.py
    cli.py
    ingest.py
    models.py
    serializers.py
    static.py
    store.py
    parsers/__init__.py
    parsers/log.py
)

PEERDIR(
    contrib/python/fastapi
    contrib/python/pydantic/pydantic-2
    contrib/python/uvicorn
    library/python/resource
)

RESOURCE_FILES(
    PREFIX log_viewer/
    static/index.html
    static/assets/app.js
)

END()

RECURSE_FOR_TESTS(
    tests
)
