PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.build_result
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/build/build_plan
)

END()
