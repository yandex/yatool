LIBRARY()

IF(YA_OPENSOURCE)
    SRCS(opensource_conf.cpp)
ELSE()
    SRCS(ya_config.cpp)
ENDIF()

END()
