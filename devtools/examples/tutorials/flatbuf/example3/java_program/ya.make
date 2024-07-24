JAVA_PROGRAM()

JDK_VERSION(11)

PEERDIR(
    contrib/java/com/google/flatbuffers/flatbuffers-java
    devtools/examples/tutorials/flatbuf/example3/library
    devtools/examples/tutorials/flatbuf/example3/page
)

JAVA_SRCS(
    **/*.java
)

LINT(base)

END()
