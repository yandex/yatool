PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.vcs.vcsversion
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/vcs
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/svn
    )
ENDIF()

END()

RECURSE(
    tests
)
