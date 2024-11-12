PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.ya_helper.common
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

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/typing
        contrib/deprecated/python/subprocess32
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
