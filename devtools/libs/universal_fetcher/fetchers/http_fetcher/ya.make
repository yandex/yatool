LIBRARY()

SRCS(
    GLOBAL http_fetcher.cpp
)

PEERDIR(
    devtools/libs/universal_fetcher/universal_fetcher
    devtools/libs/universal_fetcher/registry
    devtools/libs/universal_fetcher/utils/http
    devtools/libs/universal_fetcher/utils/checksum
    devtools/libs/universal_fetcher/utils/progress
    devtools/libs/universal_fetcher/utils/file_output
)

END()

RECURSE(
    registrar
)
