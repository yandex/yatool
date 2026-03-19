JAVA_PROGRAM(hello-resource)

JDK_VERSION(17)

JAVA_SRCS(SRCDIR src/main/java **/*.java)
JAVA_SRCS(RESOURCES SRCDIR src/main/resources **/*)

END()
