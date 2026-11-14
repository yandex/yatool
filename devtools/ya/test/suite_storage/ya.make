PY3_LIBRARY()

PY_SRCS(
    __init__.py
    context.py
    loading.py
    readonly.py
    suite_views.py
    suites.py
    views.py
)

PEERDIR(
    devtools/ya/test/suite_storage/disk
    devtools/ya/test/suite_storage/in_memory
    devtools/ya/test/test_types/common
    devtools/ya/test/common
    devtools/ya/test/facility
)

END()

RECURSE_FOR_TESTS(tests)

RECURSE(
    base
    disk
    in_memory
)
