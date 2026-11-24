JAVA_LIBRARY()


DEFAULT_JDK_VERSION(11)

JAVA_SRCS(SRCDIR src/main/java **/*)

PEERDIR_FROM_POLICY(
    ALLOW
    contrib/java/.*/ut
    library/recipes
)

END()
