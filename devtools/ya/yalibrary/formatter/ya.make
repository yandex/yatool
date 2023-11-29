PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.formatter
    __init__.py
    formatter.py
    html.py
    palette.py
    plaintext.py
    teamcity.py
    term.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/term
    library/python/color
    library/python/strings
)

END()

RECURSE(tests)
