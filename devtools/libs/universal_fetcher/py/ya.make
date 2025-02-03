PY3_LIBRARY()

PY_SRCS(
    __init__.py
    universal_fetcher.pyx
    config_builder.py
    types.py
)

PEERDIR(
    devtools/libs/universal_fetcher/universal_fetcher
    devtools/libs/universal_fetcher/registry
    devtools/libs/universal_fetcher/py/helpers
    devtools/libs/universal_fetcher/fetchers/docker_fetcher/registrar
    devtools/libs/universal_fetcher/fetchers/http_fetcher/registrar
    devtools/libs/universal_fetcher/fetchers/custom_fetcher
    devtools/libs/universal_fetcher/process_runners/direct/registrar
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/libs/universal_fetcher/fetchers/sandbox_fetcher/registrar
    )
ENDIF()

END()

RECURSE(
    helpers
)
