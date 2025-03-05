PY3_LIBRARY()

SRCDIR(devtools/ya)

PY_SRCS(
    TOP_LEVEL
    entry/entry.py
    entry/main.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/app
    devtools/ya/core/config
    devtools/ya/core/error
    devtools/ya/core/logger
    devtools/ya/core/plugin_loader
    devtools/ya/core/respawn
    devtools/ya/core/sec
    devtools/ya/core/sig_handler
    devtools/ya/core/stage_tracer
    devtools/ya/core/yarg
    library/python/mlockall
    library/python/svn_version
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/faulthandler
    )
ENDIF()

END()
