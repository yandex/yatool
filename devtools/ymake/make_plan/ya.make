LIBRARY()

PEERDIR(
    devtools/ymake/common
    library/cpp/json
)

SRCS(
    make_plan.cpp
)

IF(NEW_UID_COMPARE)
    CFLAGS(GLOBAL -DNEW_UID_COMPARE)
ENDIF()

END()
