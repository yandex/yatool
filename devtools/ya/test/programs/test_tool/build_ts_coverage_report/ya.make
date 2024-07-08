PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    build_ts_coverage_report.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/util
)

END()
