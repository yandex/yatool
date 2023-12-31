# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(1.9.0)

LICENSE(Apache-2.0)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    distro/__init__.py
    distro/__main__.py
    distro/distro.py
)

RESOURCE_FILES(
    PREFIX contrib/python/distro/py3/
    .dist-info/METADATA
    .dist-info/entry_points.txt
    .dist-info/top_level.txt
    distro/py.typed
)

END()

RECURSE(
    bin
)

RECURSE_FOR_TESTS(
    tests
)
