PY3_LIBRARY()

PY_SRCS(
    __init__.py
    client.py
)

PEERDIR(
    contrib/python/grpcio
    devtools/recipe_manager/proto
    devtools/ya/yalibrary/loggers/file_log
    library/python/filelock
    library/python/fs
    library/python/svn_version
)

END()
