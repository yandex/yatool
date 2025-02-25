PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/Jinja2

    devtools/ya/core/common_opts

    devtools/ya/yalibrary/debug_store/processor
    devtools/ya/yalibrary/evlog
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/tools

    library/python/resource
)

RESOURCE_FILES(
    data/Makefile.jinja2
    data/execute_ya.py
)

END()
