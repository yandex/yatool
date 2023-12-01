PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

IF (OS_LINUX)
    PY_SRCS(
        mount.pyx
    )
    PEERDIR(
        devtools/ya/test/programs/test_tool/lib/unshare
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
