PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.store.bazel_store
    bazel_store.py
)

PEERDIR(
    devtools/ya/core/report
    devtools/ya/exts
)

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    tests
)
