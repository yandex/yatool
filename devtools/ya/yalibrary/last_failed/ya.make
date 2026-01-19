PY3_LIBRARY()

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/test_types
    devtools/ya/yalibrary/store
    devtools/ya/yalibrary/runner
)

PY_SRCS(
    NAMESPACE yalibrary.last_failed
    last_failed.py
)

END()
