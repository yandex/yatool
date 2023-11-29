PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.ya_helper.common
    __init__.py
    fs_utils.py
    gsid.py
    logger_count.py
    run_subprocess.py
)

PEERDIR(
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
