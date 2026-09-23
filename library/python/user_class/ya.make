PY23_LIBRARY()

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/enum34
    )
ENDIF()

PY_SRCS(
    __init__.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
