PY23_LIBRARY()

SRCDIR(devtools/ya)

STYLE_PYTHON()

PY_SRCS(
    TOP_LEVEL
    entry/entry.py
    entry/main.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/app
    devtools/ya/core/sig_handler
    library/python/mlockall
    library/python/svn_version
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/faulthandler
    )
ENDIF()

END()
