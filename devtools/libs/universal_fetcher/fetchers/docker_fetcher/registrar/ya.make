LIBRARY()

SRCS(
    GLOBAL registrar.cpp
)

PEERDIR(
    devtools/libs/universal_fetcher/fetchers/docker_fetcher
    devtools/libs/universal_fetcher/registry
)

END()
