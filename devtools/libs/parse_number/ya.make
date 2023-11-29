LIBRARY()

SRCS(
    parse_number.cpp
)

END()

RECURSE(
    python
)

RECURSE_FOR_TESTS(
    ut
    ut_python
)
