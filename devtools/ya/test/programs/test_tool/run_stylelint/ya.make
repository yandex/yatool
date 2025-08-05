PY3_LIBRARY()

PY_SRCS(
    run_stylelint.py
)

PEERDIR(
    build/plugins/lib/nots/package_manager
    build/plugins/lib/nots/typescript
    devtools/ya/test/const
    devtools/ya/test/system
    devtools/ya/test/test_types
)

END()

RECURSE(
    tests
)
