LIBRARY()

SRCS(
    http_request.cpp
    extended_url.cpp
)

PEERDIR(
    library/cpp/http/simple
    library/cpp/string_utils/base64
    library/cpp/digest/md5
    library/cpp/json
    devtools/libs/universal_fetcher/utils/checksum
)

END()
