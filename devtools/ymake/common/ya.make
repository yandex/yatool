LIBRARY()

SRCS(
    content_holder.cpp
    cyclestimer.cpp
    iterable_tuple.cpp
    iter_pair.cpp
    memory_pool.cpp
    npath.cpp
    split_string.cpp
    uniq_vector.cpp
)

PEERDIR(
    library/cpp/containers/absl_flat_hash
    library/cpp/string_utils/base64
    devtools/ymake/diag
)

END()
