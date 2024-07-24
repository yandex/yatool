JAVA_LIBRARY(hello-lib)

JDK_VERSION(11)

DEFAULT_JAVA_SRCS_LAYOUT()

PEERDIR(
    devtools/examples/tutorials/java/hello-deptree/sublib
)

END()

RECURSE_FOR_TESTS(
    src/test
)
