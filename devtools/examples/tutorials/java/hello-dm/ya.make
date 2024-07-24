JAVA_PROGRAM()

JDK_VERSION(11)


JAVA_SRCS(SRCDIR src/main/java **/*.java)

PEERDIR(contrib/java/org/yaml/snakeyaml)
DEPENDENCY_MANAGEMENT(contrib/java/org/yaml/snakeyaml/1.27)

END()
