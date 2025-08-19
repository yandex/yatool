PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.store
    dist_store.py
    hash_map.py
    file_store.py
    usage_map.py
    new_store.py
    size_store.py
    lru.py
)

PEERDIR(
    contrib/python/humanfriendly
    devtools/ya/core/report
    devtools/ya/exts
    devtools/ya/yalibrary/chunked_queue
    devtools/ya/yalibrary/store/hash_map
    library/python/cityhash
    library/python/compress
    library/cpp/logger
    library/cpp/logger/global
)

END()

RECURSE(
    tests
    yt_store
    hash_map
    bazel_store
)
