LIBRARY()

IF (MSVC)
    # default flags are good
ELSE()
    SET(RAGEL6_FLAGS -lF1)
ENDIF()

PEERDIR(
    devtools/ymake/common
    devtools/ymake/yndex
    library/cpp/string_utils/subst_buf
)

SRCS(
    statement_location.cpp
    makefile_lang.rl6
)

END()
