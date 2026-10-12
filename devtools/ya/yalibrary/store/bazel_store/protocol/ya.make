PY3_LIBRARY()

PY_NAMESPACE(yalibrary.store.bazel_store.protocol)

PY_SRCS(
    __init__.py
)

END()

RECURSE_FOR_TESTS(tests)
