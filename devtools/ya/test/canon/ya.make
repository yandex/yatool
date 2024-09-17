PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    compare.py
    data.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/system/process
    devtools/ya/test/util
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/upload/consts
    devtools/ya/yalibrary/vcs
    library/python/testing/yatest_lib
    contrib/python/diff-match-patch
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/svn
        devtools/ya/test/canon/upload
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
