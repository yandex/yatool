LIBRARY()

SRCS(
    dbi.cpp
)

PEERDIR(
    library/cpp/sqlite3
)

GENERATE_ENUM_SERIALIZATION(dbi.h)

END()
