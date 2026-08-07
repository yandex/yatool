LIBRARY()

SRCS(
    GLOBAL entry.cpp
    watchdog.cpp
)

PEERDIR(
    devtools/ya/cpp/lib
    devtools/ya/cpp/lib/snowden
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
