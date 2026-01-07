PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.debug_store.processor
    __init__.py
    _common.py
    html_generator.py
)

PEERDIR(
    contrib/python/Jinja2
    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/yalibrary/evlog
    library/python/resource
)

RESOURCE_FILES(
    template.jinja2
)

END()
