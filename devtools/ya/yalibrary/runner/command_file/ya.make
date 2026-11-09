LIBRARY()
INCLUDE(${ARCADIA_ROOT}/devtools/ya_make/windows.ya.make.inc)

SRCS(
    command_file.cpp
)

END()

RECURSE(
    python
)

RECURSE_FOR_TESTS(
    tests
)
