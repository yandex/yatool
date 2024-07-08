PY3_LIBRARY()

PEERDIR(
    devtools/ya/test/common
    devtools/ya/test/filter
    devtools/ya/test/system/process
    devtools/ya/test/test_types
    devtools/ya/yalibrary/graph
)

STYLE_PYTHON()

PY_SRCS(
    run_custom_lint.py
)

END()
