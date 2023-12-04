PY23_LIBRARY()

PEERDIR(
    devtools/ya/test/const
    devtools/ya/yalibrary/display
    library/python/strings
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/backports.shutil-get-terminal-size
    )
ENDIF()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.status_view
    __init__.py
    helpers.py
    status.py
    pack.py
    term_view.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
