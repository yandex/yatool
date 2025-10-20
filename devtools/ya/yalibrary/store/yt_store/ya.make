PY3_LIBRARY()

SRCS(xx_client.cpp)

PY_SRCS(
    NAMESPACE yalibrary.store.yt_store
    CYTHON_CPP
    yt_store.py
    client.py
    consts.py
    utils.py
    retries.py
    xx_client.pyx
)

PEERDIR(
    contrib/python/contextlib2
    contrib/python/humanfriendly
    contrib/python/retrying
    contrib/python/six

    devtools/ya/core/gsid
    devtools/ya/core/report
    devtools/ya/core/monitoring
    devtools/ya/yalibrary/store

    library/cpp/regex/pcre
    library/cpp/retry
    library/cpp/threading/cancellation
    library/cpp/threading/future/subscription
    library/cpp/ucompress
    library/python/retry

    yt/cpp/mapreduce/client
    yt/cpp/mapreduce/util
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

    devtools/libs/json_sax
    devtools/ya

    yt/python
    yt/yt/build
    yt/yt/client
    yt/yt/core
    yt/yt/library
    yt/yt/python
    yt/yt_proto
    yt/cpp
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

RECURSE(opts_helper)

RECURSE_FOR_TESTS(
    tests
)
