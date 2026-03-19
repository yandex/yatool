JAVA_LIBRARY(hello-lib)

JDK_VERSION(17)

JAVA_SRCS(SRCDIR src/main/java **/*.java)

END()

RECURSE_FOR_TESTS(src/test)
