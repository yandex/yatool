PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.runner.tasks
    cache.py
    dist_cache.py
    enums.py
    pattern.py
    prepare.py
    resource_display.py
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
    devtools/libs/acdigest/python
    devtools/libs/parse_number/python
    devtools/executor/proto
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/active_state
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/fetcher/uri_parser
    devtools/ya/yalibrary/fetcher/progress_info
    devtools/ya/yalibrary/worker_threads
    devtools/ya/yalibrary/toolscache
    contrib/python/six
)

END()
