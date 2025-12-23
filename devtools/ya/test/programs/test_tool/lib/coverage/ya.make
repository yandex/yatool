PY3_LIBRARY()

PY_SRCS(
    __init__.py
    export.py
    jacoco_report.py
    util.py
)

PEERDIR(
    contrib/deprecated/python/ujson
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage/iter_cov_json/lib
    devtools/ya/test/programs/test_tool/lib/coverage/merge
    library/python/cityhash
    library/python/reservoir_sampling
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        contrib/tools/sancov
    )
ENDIF()

END()

RECURSE(
    iter_cov_json
    merge
)

RECURSE_FOR_TESTS(
    tests
)
