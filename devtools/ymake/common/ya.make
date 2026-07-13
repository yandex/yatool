LIBRARY()

SRCS(
    content_holder.cpp
    iterable_tuple.cpp
    iter_pair.cpp
    memory_pool.cpp
    npath.cpp
    json_writer.cpp
    uniq_vector.cpp
)

PEERDIR(
    library/cpp/containers/absl
    library/cpp/string_utils/base64
)

END()
