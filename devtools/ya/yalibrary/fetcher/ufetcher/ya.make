PY3_LIBRARY()

PEERDIR(
    devtools/ya/exts
    devtools/ya/core/config
    devtools/ya/app_config
    devtools/experimental/universal_fetcher/py
)

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.fetcher.ufetcher
    __init__.py
)

END()
