PROTO_LIBRARY()

GRPC()

PEERDIR(
    devtools/local_cache/psingleton/proto
)

SRCS(
    tools.proto
)

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

END()
