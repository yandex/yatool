SUBSCRIBER(g:yatool)

# This is not a real library. PY3_LIBRARY is only used to run style and python linters.
PY3_LIBRARY()

PY_SRCS(
    graph_executor.py
    run_bootstrap.py
)

STYLE_PYTHON()

END()
