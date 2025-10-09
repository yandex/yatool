PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.vcs.vcsversion
    __init__.py
    standalone.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/yalibrary/vcs
)

IF (PYTHON2)
    PEERDIR(
        vcs/svn/run
    )
    IF (NOT YA_OPENSOURCE)
        PEERDIR(
            devtools/ya/yalibrary/svn
        )
    ENDIF()
ENDIF()

END()

RECURSE(
    tests
)
