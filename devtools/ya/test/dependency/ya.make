PY23_LIBRARY()

PY_SRCS(
    external_tools.py
    mds_storage.py
    sandbox_resource.py
    testdeps.py
    uid.py
)

PEERDIR(
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/test/system/env
    devtools/ya/test/util
    devtools/ya/yalibrary/large_files
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/yandex/distbuild/distbs_consts
)

IF(NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/svn
    )
ENDIF()

END()

RECURSE(
    sandbox_storage
)

RECURSE_FOR_TESTS(
    tests
)
