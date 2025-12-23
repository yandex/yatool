JAVA_LIBRARY()

IF(YA_IDE_GRADLE != "yes")
    DISABLE(OPENSOURCE_EXPORT)
ENDIF()

DEFAULT_JDK_VERSION(11)

JAVA_SRCS(
    SRCDIR src/main/java **/*.java
)

INCLUDE(${ARCADIA_ROOT}/contrib/java/org/junit/junit-bom/5.12.2/ya.dependency_management.inc)

PEERDIR(
    devtools/junit5-runner/junit_common
)

LINT(base)
END()
