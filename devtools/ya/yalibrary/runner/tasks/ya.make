PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.runner.tasks
    cache.py
    dist_cache.py
    pattern.py
    prepare.py
    resource.py
    result.py
    run.py
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/runner/tasks/distbuild
    )
ENDIF()

PEERDIR(
    devtools/libs/parse_number/python
    devtools/executor/proto
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/worker_threads
    devtools/ya/yalibrary/toolscache
    contrib/python/six
)

END()
