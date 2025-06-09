PROGRAM()

STRIP()

PEERDIR(
    library/cpp/getopt
    library/cpp/yson/node
    yt/cpp/mapreduce/client
    yt/cpp/mapreduce/interface
    yt/cpp/mapreduce/interface/logging
    yt/cpp/mapreduce/util
)

SRCS(
    main.cpp
    cleaner.cpp
    logger.cpp
)

END()
