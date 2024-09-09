PY23_LIBRARY()

PEERDIR(
    contrib/python/contextlib2
    contrib/python/toolz
    contrib/python/requests
    devtools/ya/exts
    devtools/ya/core/config
    devtools/ya/yalibrary/fetcher/uri_parser
    devtools/ya/yalibrary/guards
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/toolscache
    devtools/libs/yaplatform/python
)

IF (PYTHON3 AND NOT OS_WINDOWS)
    PEERDIR(
        devtools/ya/yalibrary/fetcher/ufetcher
    )
ENDIF()

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/yandex/sandbox
    )
ENDIF()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.fetcher
    __init__.py
    cache_helper.py
    common.py
    fetchers_storage.py
    resource_fetcher.py
    tool_chain_fetcher.py
)

END()

RECURSE(
    ufetcher
)

RECURSE_FOR_TESTS(
    tests
)
