PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    fs_utils.py
    gsid.py
    logger_count.py
    run_subprocess.py
)

PEERDIR(
    devtools/ya/core/sec
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    tests
)
