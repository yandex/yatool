PY23_LIBRARY()

PEERDIR(
    contrib/python/contextlib2
    contrib/python/toolz
    contrib/python/requests
    devtools/ya/exts
    devtools/ya/core/config
    devtools/ya/yalibrary/guards
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/toolscache
    devtools/libs/yaplatform/python
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/yandex/sandbox
    )
ENDIF()

IF (NEBIUS)
    PEERDIR(
        devtools/ya/yalibrary/yandex/sandbox
        devtools/ya/yalibrary/tasklet_resources_fetcher
    )
ENDIF()

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

RECURSE_FOR_TESTS(
    tests
)
