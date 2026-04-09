PY3_LIBRARY()

PY_SRCS(
    __init__.py
    CYTHONIZE_PY
    branch.py
    consts.py
    functions.py
    mcdc.py
    sancov.py
    segments.py
    shared.py
)

END()
