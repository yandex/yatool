LIBRARY()

SRCS(
    GLOBAL http_fetcher.cpp
)

PEERDIR(
    devtools/experimental/universal_fetcher/universal_fetcher
    devtools/experimental/universal_fetcher/registry
    devtools/experimental/universal_fetcher/utils/http
    devtools/experimental/universal_fetcher/utils/checksum
)

END()

RECURSE(
    registrar
)
