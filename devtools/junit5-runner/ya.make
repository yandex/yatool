JAR_LIBRARY()

IF(YA_IDE_GRADLE != "yes")
    DISABLE(OPENSOURCE_EXPORT)
ENDIF()

PROVIDES(junit-runner)

DEFAULT_JDK_VERSION(11)

JAVA_SRCS(
    SRCDIR src/main/java **/*.java
)

INCLUDE(${ARCADIA_ROOT}/contrib/java/org/junit/junit-bom/5.5.2/ya.dependency_management.inc)

DEPENDENCY_MANAGEMENT(
    contrib/java/com/google/code/gson/gson/2.8.6
    contrib/java/com/beust/jcommander/1.72
)

PEERDIR(
    devtools/jtest
    devtools/jtest-annotations/junit5

    contrib/java/org/junit/platform/junit-platform-launcher
    contrib/java/org/junit/jupiter/junit-jupiter

    contrib/java/org/opentest4j/opentest4j/1.2.0
)

LINT(base)
END()

RECURSE_FOR_TESTS(
    test-pack/test
    test-pack/logs-jul
    test-pack/logs-log4j
    test-pack/logs-log4j2
    test-pack/logs-logback
    src/test
)
