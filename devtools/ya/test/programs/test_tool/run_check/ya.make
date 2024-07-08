PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    run_check.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/system
    devtools/ya/test/test_types
    devtools/ya/test/util
    library/python/cores
)

END()
