LIBRARY()

SRCS(
    name_store.cpp
    name_data_store.cpp
)

PEERDIR(
    devtools/ymake/common
    library/cpp/containers/absl_flat_hash
    library/cpp/on_disk/multi_blob
)

END()

RECURSE_FOR_TESTS(ut)
