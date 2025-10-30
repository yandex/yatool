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

    # Multiple implementations
    devtools/junit5-runner/junit_common
    devtools/junit5-runner/junit_5_12
    devtools/junit5-runner/junit_5_13
)

LINT(base)
END()

RECURSE(
    junit_common
    junit_5_12
    junit_5_13
)

RECURSE_FOR_TESTS(
    test-pack/test
    test-pack/logs-jul
    test-pack/logs-log4j
    test-pack/logs-log4j2
    test-pack/logs-logback
    test-pack_5_12/test
    test-pack_5_12/logs-jul
    test-pack_5_12/logs-log4j
    test-pack_5_12/logs-log4j2
    test-pack_5_12/logs-logback
    test-pack_5_13/test
    test-pack_5_13/logs-jul
    test-pack_5_13/logs-log4j
    test-pack_5_13/logs-log4j2
    test-pack_5_13/logs-logback
    test-example/test
    src/test
    src/test_5_12
    src/test_5_13
)
