PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.store.yt_store
    yt_store.py
    proxy.py
    client.py
    configuration.py
    consts.py
    utils.py
)

PEERDIR(
    contrib/python/contextlib2
    contrib/python/humanfriendly
    contrib/python/retrying
    contrib/python/six
    devtools/ya/core/gsid
    devtools/ya/core/report
    devtools/ya/yalibrary/store
    yt/python/client_lite
    yt/python/yt/yson
    yt/yt/python/yt_yson_bindings
)

CHECK_DEPENDENT_DIRS(
    ALLOW_ONLY
    ALL

    build
    certs
    contrib
    library
    tools
    util

    devtools/ya

    yt/python
    yt/yt/build
    yt/yt/core
    yt/yt/library
    yt/yt/python
    yt/yt_proto
)

IF (NOT OPENSOURCE)
    CHECK_DEPENDENT_DIRS(
        ALLOW_ONLY
        ALL

        security
        yt/python_py2
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
