PY3_LIBRARY()

PY_SRCS(
    error.py
    main.py
    mp_logging.py
    sentinel.py
    service.py
    subreaper.py
)

PEERDIR(
    contrib/python/grpcio
    contrib/python/grpcio-reflection
    contrib/python/psutil
    devtools/recipe_manager/client
    devtools/recipe_manager/proto
    library/python/filelock
    library/python/fs
)

IF (OS_LINUX)
    PEERDIR(
        library/python/prctl
    )
ENDIF()

END()

RECURSE(
    bin
)
