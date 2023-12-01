PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE test.test_node
    __init__.py
)

PY_SRCS(
    cmdline.py
    fuzzing.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/build/gen_plan
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/test/canon
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/dependency
    devtools/ya/test/error
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/system
    devtools/ya/test/test_node/coverage
    devtools/ya/test/test_types
    devtools/ya/test/util
    devtools/ya/yalibrary/last_failed
    devtools/ya/yalibrary/upload/consts
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/test/test_node/sandbox
        devtools/ya/test/dependency/sandbox_storage
    )
ENDIF()

END()
