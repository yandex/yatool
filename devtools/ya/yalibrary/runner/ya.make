PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.runner

    __init__.py
    build_root.py
    lru_store.py
    patterns.py
    result_store.py
    ring_store.py
    runner3.py
    statcalc.py
    task_cache.py
    topo.py
    uid_store.py
    runqueue.py
    timeline_store.py
)

PEERDIR(
    devtools/libs/limits/python
    devtools/executor/python
    devtools/ya/app_config
    devtools/ya/exts
    devtools/ya/yalibrary/active_state
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/runner/command_file/python
    devtools/ya/yalibrary/runner/fs
    devtools/ya/yalibrary/runner/sandboxing
    devtools/ya/yalibrary/runner/tasks
    devtools/ya/yalibrary/status_view
    devtools/ya/yalibrary/status_view
    devtools/ya/yalibrary/store
    devtools/ya/yalibrary/worker_threads
    library/python/reservoir_sampling
)

END()

RECURSE(
    command_file
    sandboxing
    tasks
    tests
    fs
)
