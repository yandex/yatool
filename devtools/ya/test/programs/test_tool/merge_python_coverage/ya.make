PY23_LIBRARY()

PY_SRCS(
    merge_python_coverage.py
)

PEERDIR(
    contrib/python/coverage
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage
)

END()
