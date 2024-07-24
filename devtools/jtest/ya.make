JAVA_LIBRARY()

IF(JDK_VERSION == "")
    JDK_VERSION(11)
ENDIF()

JAVA_SRCS(SRCDIR src/main/java **/*.java)

PEERDIR(
    contrib/java/com/google/code/gson/gson/2.8.9
    contrib/java/com/beust/jcommander/1.72
)

LINT(base)
END()

RECURSE_FOR_TESTS(
    src/test
)
