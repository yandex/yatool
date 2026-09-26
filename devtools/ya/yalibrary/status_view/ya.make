PY23_LIBRARY()

PEERDIR(
    devtools/ya/core/error
    devtools/ya/test/const
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/roman
    library/python/strings
    library/python/func
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/backports.shutil-get-terminal-size
    )
ENDIF()

PY_SRCS(
    NAMESPACE yalibrary.status_view
    __init__.py
    helpers.py
    jsonl.py
    pacer.py
    status.py
    pack.py
    plain.py
    term_view.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
