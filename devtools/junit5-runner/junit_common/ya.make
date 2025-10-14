JAVA_LIBRARY()

IF(YA_IDE_GRADLE != "yes")
    DISABLE(OPENSOURCE_EXPORT)
ENDIF()

DEFAULT_JDK_VERSION(11)

JAVA_SRCS(
    SRCDIR src/main/java **/*.java
)

INCLUDE(${ARCADIA_ROOT}/contrib/java/org/junit/junit-bom/5.5.2/ya.dependency_management.inc)

PEERDIR(
    devtools/jtest

    contrib/java/org/junit/jupiter/junit-jupiter
    contrib/java/org/junit/platform/junit-platform-launcher
)

LINT(base)
END()
