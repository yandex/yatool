PY3_LIBRARY()

PY_SRCS(
    __init__.py
    behaviour.py
    dispatch.py
    handler.py
    params.py
    aliases.py
    config_files.py
    consumers.py
    excs.py
    groups.py
    help.py
    help_level.py
    hooks.py
    options.py
    populate.py
)

PEERDIR(
    contrib/python/pylev
    contrib/python/toml
    devtools/ya/app_config
    devtools/ya/core/config
    devtools/ya/core/report
    devtools/ya/exts
    devtools/ya/yalibrary/display
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/typing
    )
ENDIF()

END()

RECURSE(
    testlib
)

RECURSE_FOR_TESTS(
    tests
)
