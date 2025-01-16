PY23_LIBRARY()

PY_SRCS(
    __init__.py
    identity.py
)

IF (OS_LINUX)
    PEERDIR(
        library/python/nstools
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
