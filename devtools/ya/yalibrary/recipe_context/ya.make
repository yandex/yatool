PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.recipe_context
    __init__.py
)

PEERDIR(library/python/unique_id)

END()

RECURSE_FOR_TESTS(ut)
