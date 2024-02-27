PY23_LIBRARY()

STYLE_PYTHON()

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
