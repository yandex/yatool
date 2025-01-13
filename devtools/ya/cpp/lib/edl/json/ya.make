LIBRARY()

SRCS(
    from_json.cpp
    to_json.cpp
)

PEERDIR(
    devtools/ya/cpp/lib/edl/common
    devtools/libs/json_sax
    library/cpp/json
    library/cpp/json/common
    library/cpp/json/fast_sax
)

END()

RECURSE_FOR_TESTS(
    ut
)
