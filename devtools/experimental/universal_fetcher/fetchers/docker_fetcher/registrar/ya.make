LIBRARY()

SRCS(
    GLOBAL registrar.cpp
)

PEERDIR(
    devtools/experimental/universal_fetcher/fetchers/docker_fetcher
    devtools/experimental/universal_fetcher/registry
)

END()
