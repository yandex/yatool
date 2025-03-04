# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(0.3.6)

LICENSE(BSD-3-Clause)

PEERDIR(
    contrib/libs/re2
)

ADDINCL(
    contrib/python/pyre2/py3/src
)

NO_COMPILER_WARNINGS()

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    CYTHON_CPP
    src/re2.pyx=re2
)

RESOURCE_FILES(
    PREFIX contrib/python/pyre2/py3/
    .dist-info/METADATA
    .dist-info/top_level.txt
)

END()

RECURSE_FOR_TESTS(
    tests
)
