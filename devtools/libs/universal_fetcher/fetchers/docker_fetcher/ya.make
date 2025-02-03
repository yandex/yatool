LIBRARY()

SRCS(
    GLOBAL docker_fetcher.cpp
)

PEERDIR(
    devtools/libs/universal_fetcher/universal_fetcher
    devtools/libs/universal_fetcher/registry
)

END()

RECURSE(
    registrar
    skopeo_static
)
