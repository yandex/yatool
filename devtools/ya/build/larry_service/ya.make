PY3_LIBRARY()

PY_SRCS(
    __init__.py
    address.py
    client.py
    compatibility.py
    release.py
    installation.py
    paths.py
    configuration.py
    credentials.py
    diagnostics.py
    state_files.py
    manager.py
    controller.py
    launchd.py
    systemd.py
    recovery.py
)

PEERDIR(
    devtools/local_cache/psingleton/python
    devtools/ya_make/libs/larry_client/py
    devtools/ya/exts
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/tools
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(devtools/ya/yalibrary/yandex/sandbox)
ENDIF()

END()

RECURSE_FOR_TESTS(
    it
    tests
)
