PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.debug_store
    __init__.py
)

PEERDIR(
    devtools/ya/yalibrary/debug_store/store
    devtools/ya/yalibrary/debug_store/processor
)

END()

RECURSE(
    processor
    store
)

RECURSE_FOR_TESTS(
    tests
)
