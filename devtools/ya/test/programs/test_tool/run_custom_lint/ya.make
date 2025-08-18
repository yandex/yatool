PY3_LIBRARY()

PEERDIR(
    devtools/ya/test/common
    devtools/ya/test/filter
    devtools/ya/test/system/process
    devtools/ya/test/test_types
    devtools/ya/yalibrary/graph
    # These are needed to run wrapper scripts
    build/plugins/lib/test_const
    library/python/testing/custom_linter_util
    library/python/testing/style
)

PY_SRCS(
    run_custom_lint.py
)

END()
