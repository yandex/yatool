LIBRARY()

SRCS(
    GLOBAL entry.cpp
    watchdog.cpp
)

PEERDIR(
    devtools/ya/cpp/lib
    # Add handlers here
    devtools/ya/cpp/handlers/gc
    devtools/ya/cpp/handlers/tool
)

END()

RECURSE(
    bin
)

RECURSE_FOR_TESTS(
    tests
)
