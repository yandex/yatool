PROTO_LIBRARY()

GRPC()

PEERDIR(
    devtools/local_cache/psingleton/proto
)

SRCS(
    ac.proto
)

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

END()
