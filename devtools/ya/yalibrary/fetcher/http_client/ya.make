PY23_LIBRARY()

PEERDIR(
    devtools/ya/exts
)

IF (PYTHON3 AND NOT OS_WINDOWS AND NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/libs/universal_fetcher/py
    )
ENDIF()

PY_SRCS(
    NAMESPACE yalibrary.fetcher.http_client
    __init__.py
)

END()
