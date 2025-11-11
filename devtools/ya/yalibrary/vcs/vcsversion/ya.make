PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.vcs.vcsversion
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/find_root
)

END()

RECURSE(
    tests
)
