LIBRARY()

SRCS(
    GLOBAL docker_fetcher.cpp
)

PEERDIR(
    devtools/experimental/universal_fetcher/universal_fetcher
    devtools/experimental/universal_fetcher/registry
)

END()

RECURSE(
    registrar
    skopeo_static
)
