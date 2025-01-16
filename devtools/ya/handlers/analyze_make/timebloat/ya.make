PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/tqdm
    devtools/ya/handlers/analyze_make/timebloat/html
    devtools/ya/core/resource
    devtools/ya/exts
    devtools/ya/test/filter
    devtools/ya/tools/analyze_make/common
    library/python/resource
)

END()
