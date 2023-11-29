PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.store
    hash_map.py
    file_store.py
    usage_map.py
    new_store.py
    size_store.py
    lru.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/core/report
    library/python/cityhash
    library/python/compress
    devtools/ya/yalibrary/chunked_queue
    devtools/ya/yalibrary/store/hash_map
)

END()

RECURSE(
    tests
    yt_store
    hash_map
    bazel_store
)
