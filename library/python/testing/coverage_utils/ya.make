PY23_LIBRARY()

PY_SRCS(__init__.py)

PEERDIR_FROM_POLICY(
    ALLOW
    contrib/python/coverage
)

END()
