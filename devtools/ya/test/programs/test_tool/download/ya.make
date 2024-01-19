PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    download.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/core
    devtools/ya/test/dependency/sandbox_storage
    devtools/ya/test/util
)

END()

RECURSE_FOR_TESTS(tests)
