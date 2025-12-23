PY23_LIBRARY()

PY_SRCS(
    __init__.py
    allure_support.py
    console.py
    dry.py
    junit.py
    report_prototype.py
    stderr_reporter.py
    trace_comment.py
    transformer.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/term
    devtools/ya/yalibrary/tools
    library/python/strings
)

END()

RECURSE_FOR_TESTS(
    tests
)
