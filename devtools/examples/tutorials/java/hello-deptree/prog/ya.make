JAVA_PROGRAM(hello-prog)

JDK_VERSION(11)

DEFAULT_JAVA_SRCS_LAYOUT()

PEERDIR(
    devtools/examples/tutorials/java/hello-deptree/lib
)

END()

RECURSE_FOR_TESTS(
    src/test
)
