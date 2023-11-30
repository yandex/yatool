PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(resolve_ts_coverage.py)

PEERDIR(
    contrib/deprecated/python/ujson
    devtools/ya/test/programs/test_tool/lib/runtime
    devtools/ya/test/util
)

END()
