PROGRAM()

PEERDIR(
    devtools/examples/tutorials/protobuf/example4/helloworld
    contrib/libs/grpc
    contrib/libs/grpc/grpc++_reflection
)

SRCS(
    greeter_server.cc
)

END()
