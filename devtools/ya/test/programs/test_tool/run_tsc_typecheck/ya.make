PY3_LIBRARY()

PY_SRCS(
    parse_output.py
    run_tsc_typecheck.py
)

PEERDIR(
    library/python/color
    build/plugins/lib/nots/package_manager
    build/plugins/lib/nots/test_utils
    build/plugins/lib/nots/typescript
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/test/system
    devtools/ya/test/test_types
    devtools/ya/test/util
)

END()

RECURSE(
    tests
)
