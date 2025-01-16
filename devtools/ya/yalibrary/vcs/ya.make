PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.vcs
    __init__.py
)

PEERDIR(
    library/python/find_root
)

END()

RECURSE(
    vcsversion
    arc
)
