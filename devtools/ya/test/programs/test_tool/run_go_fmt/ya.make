PY3_LIBRARY()

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/system/process
    devtools/ya/test/test_types
    devtools/ya/test/util
    library/python/cores
    contrib/python/six
)

PY_SRCS(
    run_go_fmt.py
)

END()
