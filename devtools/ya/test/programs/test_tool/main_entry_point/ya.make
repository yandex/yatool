PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(__init__.py)

PEERDIR(
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/formatter
    devtools/ya/test/programs/test_tool/lib
)

END()
