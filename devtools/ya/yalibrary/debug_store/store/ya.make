PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.debug_store.store
    __init__.py
)

IF (PYTHON2)
    PEERDIR(
        contrib/python/pathlib2
        contrib/deprecated/python/typing
        contrib/deprecated/python/enum34
    )
ENDIF()

PEERDIR(
    devtools/ya/core/config
    devtools/ya/exts
)

END()
