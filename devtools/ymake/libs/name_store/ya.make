LIBRARY()

SRCS(
    name_store.cpp
    name_data_store.cpp
)

PEERDIR(
    devtools/ymake/common
    library/cpp/containers/absl
    library/cpp/on_disk/multi_blob
    library/cpp/threading/light_rw_lock
)

END()

RECURSE_FOR_TESTS(ut)
