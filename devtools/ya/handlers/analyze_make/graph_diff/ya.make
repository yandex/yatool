PY3_LIBRARY()

PEERDIR(
    devtools/ya/app
    devtools/ya/build/graph_description
    devtools/ya/core/common_opts
    devtools/ya/core/yarg
    library/python/json
)

PY_SRCS(
    __init__.py
    compare.py
    find_diff.py
)

END()
