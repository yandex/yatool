PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.debug_store.processor
    __init__.py
    _common.py
    html_generator.py
)

IF (PYTHON2)
    PEERDIR(
        contrib/python/pathlib2
        contrib/deprecated/python/typing
        contrib/deprecated/python/enum34
    )
ENDIF()

PEERDIR(
    contrib/python/Jinja2
    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/yalibrary/evlog
)

RESOURCE_FILES(
    template.jinja2
)

END()
