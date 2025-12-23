PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.debug_store.store
    __init__.py
)

PEERDIR(
    devtools/ya/core/config
    devtools/ya/exts
)

END()
