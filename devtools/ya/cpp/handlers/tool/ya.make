LIBRARY()

SRCS(
    GLOBAL tool.cpp
    options.cpp
    toolchain.cpp
    toolchain_helpers.cpp
    GLOBAL toolchain_by_platform.cpp
    GLOBAL toolchain_latest_matched.cpp
    GLOBAL toolchain_sandbox_id.cpp
    toolscache.cpp
)

PEERDIR(
    devtools/libs/yaplatform
    devtools/local_cache/toolscache/dbbe
    devtools/local_cache/toolscache/proto
    devtools/ya/cpp/lib
)

END()

RECURSE_FOR_TESTS(
    tests
    ut
)
