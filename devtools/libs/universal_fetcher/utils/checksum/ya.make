LIBRARY()

SRCS(
    checksum.cpp
    sha512.cpp
)

GENERATE_ENUM_SERIALIZATION(checksum.h)

PEERDIR(
    library/cpp/json
    library/cpp/digest/md5
    library/cpp/string_utils/base64
    library/cpp/openssl/crypto
)

END()
