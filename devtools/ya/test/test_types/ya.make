PY23_LIBRARY()

PY_SRCS(
    benchmark.py
    boost_test.py
    clang_tidy.py
    common.py
    cov_test.py
    custom_lint.py
    detekt.py
    ext_resource.py
    fuzz_test.py
    go_test.py
    gtest.py
    iwyu.py
    java_style.py
    junit.py
    library_ut.py
    py_test.py
    ts_test.py
)

PEERDIR(
    build/plugins/lib
    contrib/python/six
    contrib/python/sortedcontainers
    devtools/ya/exts
    devtools/ya/jbuild/commands
    devtools/ya/jbuild/gen/actions
    devtools/ya/jbuild/gen/makelist_parser2
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/dependency
    devtools/ya/test/facility
    devtools/ya/test/system/env
    devtools/ya/test/system/process # TODO get rid off
    devtools/ya/test/test_node/cmdline
    devtools/ya/test/tracefile
    devtools/ya/test/util
    devtools/ya/yalibrary/graph
    devtools/ya/yalibrary/platform_matcher
)

END()
