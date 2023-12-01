PY23_LIBRARY()

STYLE_PYTHON()

TEST_SRCS(
    conftest.py
    exectest.py
)

PY_SRCS(
    run_exectest.py
)

PEERDIR(
    library/python/pytest
)

END()
