PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.imprint
    __init__.py
    base.py
    change_list.py
    imprint.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/test/error
    # devtools/ya/yalibrary/monitoring
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/core/imprint/atd
    )
ENDIF()

END()
