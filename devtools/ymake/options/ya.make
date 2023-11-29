LIBRARY()

SRCS(
    commandline_options.cpp
    debug_options.cpp
    roots_options.cpp
    startup_options.cpp
    static_options.cpp
)

PEERDIR(
    devtools/ymake/common
    devtools/ymake/diag
    library/cpp/getopt/small
)

END()
