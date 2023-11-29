LIBRARY()

PEERDIR(
    library/cpp/json
)

SRCS(
    builtin.cpp
    yndex.cpp
)

GENERATE_ENUM_SERIALIZATION(yndex.h)

END()
