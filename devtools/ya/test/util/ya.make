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
    shared.py
    tools.py
    CYTHONIZE_PY
    path.py
)

PEERDIR(
    devtools/ya/core/config
    devtools/ya/test/const
    devtools/ya/test/system/process
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/loggers/file_log
    devtools/ya/yalibrary/term
    library/python/coredump_filter
    library/python/cores
    library/python/eq_interval_sampling
)

END()
