PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/PyYAML
    contrib/python/marisa-trie
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    tests
)
