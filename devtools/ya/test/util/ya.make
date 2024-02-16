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

STYLE_PYTHON()

END()
