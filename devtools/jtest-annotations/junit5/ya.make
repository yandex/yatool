JAVA_LIBRARY()

IF(YA_IDE_GRADLE != "yes")
    DISABLE(OPENSOURCE_EXPORT)
ENDIF()

DEFAULT_JDK_VERSION(11)

INCLUDE(${ARCADIA_ROOT}/contrib/java/org/junit/junit-bom/5.5.2/ya.dependency_management.inc)

PEERDIR(contrib/java/org/junit/jupiter/junit-jupiter-api)

JAVA_SRCS(SRCDIR src/main/java **/*)

END()
