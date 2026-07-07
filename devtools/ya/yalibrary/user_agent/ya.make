PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.user_agent
    __init__.py
)

PEERDIR(
    library/python/svn_version
)

END()
