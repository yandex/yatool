PY23_LIBRARY()

PY_SRCS(__init__.py)

PEERDIR(
    build/plugins/lib
    contrib/python/six
    contrib/python/sortedcontainers
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/dependency
    devtools/ya/test/facility
    devtools/ya/test/system/process
    devtools/ya/test/tracefile
    devtools/ya/test/util
    library/python/func
    library/python/windows
)

END()
