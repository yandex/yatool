PY23_LIBRARY()

PY_SRCS(
    NAMESPACE test.util
    shared.py
    tools.py
    CYTHONIZE_PY
    path.py
)

PEERDIR(
    devtools/ya/test/const
    devtools/ya/test/system/process
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/loggers
    devtools/ya/yalibrary/loggers/file_log
    devtools/ya/yalibrary/term
    library/python/coredump_filter
    library/python/cores
)

IF (NOT YA_OPENSOURCE AND NOT NEBIUS)
    PEERDIR(
        build/internal/plugins/_lib/nots
    )
    PY_SRCS(
        NAMESPACE test.util
        ts_utils.py
    )
ENDIF()

STYLE_PYTHON()

END()
