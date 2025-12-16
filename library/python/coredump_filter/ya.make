PY23_LIBRARY()

STYLE_PYTHON()

ALL_PY_SRCS()

RESOURCE_FILES(
    PREFIX library/python/coredump_filter/
    core_proc.js
    epilog.html
    prolog.html
    styles.css
)

PEERDIR(
    contrib/python/six
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/enum34
    )
ENDIF()

END()

RECURSE(
    tests
)
