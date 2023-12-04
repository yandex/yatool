PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.makelists
    __init__.py
    com_handler.py
    macro_definitions.py
    mk_builder.py
    mk_common.py
    mk_parser.py
)

NO_LINT()

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/typing
    )
ENDIF()

END()

RECURSE(
    tests
)
