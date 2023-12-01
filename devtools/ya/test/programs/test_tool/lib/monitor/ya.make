PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    memproctree.py
    pollmon.py
    stat_tmpfs.py
)

IF (OS_LINUX)
    PY_SRCS(
        statvfs.pyx
    )
ENDIF()

PEERDIR(
    contrib/python/psutil
    contrib/python/six
    devtools/ya/yalibrary/formatter
)

END()
