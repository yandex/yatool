PY23_LIBRARY()

IF (PYTHON3)
    # ymake test-planning workers load this module under their own GIL.
    CYTHON_FLAGS(
        -X
        subinterpreters_compatible=own_gil
    )
    CFLAGS(
        -DCYTHON_USE_MODULE_STATE=1
        -DCYTHON_USE_TYPE_SPECS=1
    )
ENDIF()

PY_SRCS(
    config.pyx
)

PEERDIR(
    devtools/ya/app_config/lib
)

END()
