LIBRARY()

PEERDIR(
    contrib/libs/asio
    library/cpp/json
    library/cpp/on_disk/multi_blob
    library/cpp/protobuf/json
    library/cpp/containers/absl_flat_hash
    devtools/ymake/diag/common_msg
)

SRCS(
    display.cpp
    trace.cpp
)

END()
