PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.store.bazel_store
    bazel_store.py
)

PEERDIR(
    devtools/ya/core/report
    devtools/ya/exts
    devtools/ya/yalibrary/store
    contrib/python/zstandard
)

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    tests
)
