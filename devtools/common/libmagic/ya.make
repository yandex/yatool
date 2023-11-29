PY23_LIBRARY()

PY_SRCS(
    __init__.py
)

IF (OS_LINUX)
    PY_SRCS(
        magic.pyx
    )

    PEERDIR(
        devtools/common/libmagic/lib
    )
ENDIF()

END()

RECURSE(
    lib
)
