PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.store.bazel_store
    bazel_store.py
)

PEERDIR(
    devtools/ya/core/report
    devtools/ya/exts
    devtools/ya/yalibrary/store
    devtools/ya/yalibrary/store/bazel_store/protocol
    contrib/python/zstandard
    contrib/python/requests
    contrib/python/urllib3
)

END()

RECURSE(protocol)

RECURSE_FOR_TESTS(
    tests
    ut/http
    ut/repair
    ut/restore
)
