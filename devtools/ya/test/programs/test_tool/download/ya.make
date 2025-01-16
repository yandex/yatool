PY3_LIBRARY()

PY_SRCS(
    download.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/core/error
    devtools/ya/test/dependency/sandbox_storage
    devtools/ya/test/util
)

END()

RECURSE_FOR_TESTS(
    tests
)
