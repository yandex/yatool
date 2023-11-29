PY23_LIBRARY()

IF (NEBIUS AND YA_OPENSOURCE)
    COPY_FILE(nebius_config.py __init__.py)
ELSEIF (YA_OPENSOURCE)
    COPY_FILE(opensource_config.py __init__.py)
ELSE()
    COPY_FILE(ya_config.py __init__.py)
ENDIF()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE app_config
    __init__.py
)

END()
