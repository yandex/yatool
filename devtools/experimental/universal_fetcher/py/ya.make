PY3_LIBRARY()

PY_SRCS(
    __init__.py
    universal_fetcher.pyx
    config_builder.py
    types.py
)

PEERDIR(
    devtools/experimental/universal_fetcher/registry
    devtools/experimental/universal_fetcher/py/helpers
    devtools/experimental/universal_fetcher/fetchers/docker_fetcher/registrar
    devtools/experimental/universal_fetcher/fetchers/http_fetcher/registrar
    devtools/experimental/universal_fetcher/fetchers/custom_fetcher
    devtools/experimental/universal_fetcher/process_runners/direct/registrar
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/experimental/universal_fetcher/fetchers/sandbox_fetcher/registrar
    )
ENDIF()

END()

RECURSE(
    helpers
)
