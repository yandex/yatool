LIBRARY()

PEERDIR(
    devtools/libs/acdigest
    devtools/local_cache/ac/proto
    devtools/local_cache/common/fs-utils
    library/cpp/digest/md5
    library/cpp/openssl/crypto
)

SRCS(
    fs_blobs.cpp
)

END()

RECURSE_FOR_TESTS(ut)
