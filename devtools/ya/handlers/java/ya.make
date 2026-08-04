PY3_LIBRARY()

PY_SRCS(
    __init__.py
    dep_tree.py
    helpers.py
    opts.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/core/yarg
    devtools/ya/build
    devtools/ya/build/build_opts
    devtools/ya/handlers/java/html
    devtools/ya/test/opts
    library/python/resource
)

END()

RECURSE_FOR_TESTS(
    bin
    tests
)
