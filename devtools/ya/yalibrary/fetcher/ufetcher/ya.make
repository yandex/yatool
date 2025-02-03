PY3_LIBRARY()

PEERDIR(
    devtools/ya/exts
    devtools/ya/core/config
    devtools/ya/app_config
    devtools/libs/universal_fetcher/py
)

PY_SRCS(
    NAMESPACE yalibrary.fetcher.ufetcher
    __init__.py
)

END()
