PY23_LIBRARY()

PY_SRCS(
    NAMESPACE build.gen_plan

    __init__.py
)

PEERDIR(
    devtools/ya/build/ymake2
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/test/dependency
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/vcsversion
    devtools/ya/yalibrary/yandex/distbuild/distbs_consts
)

IF(NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/svn
    )
ENDIF()

END()
