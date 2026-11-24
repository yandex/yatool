LIBRARY()

SRCS(
    mapped_file.cpp
)

PEERDIR_FROM_POLICY(
    ALLOW
    contrib/libs/leveldb
)

END()
