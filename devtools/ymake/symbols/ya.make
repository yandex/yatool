LIBRARY()

SRCS(
    globs.cpp
    name_store.cpp
    name_data_store.cpp
    symbols.cpp
    time_store.cpp
    file_store.cpp
    cmd_store.cpp
    readdir.cpp
    base2fullnamer.cpp
    sortedreaddir.cpp
)

PEERDIR(
    devtools/ymake/common
    devtools/ymake/options
    devtools/ymake/diag
    library/cpp/containers/absl_flat_hash
    library/cpp/deprecated/autoarray
    library/cpp/on_disk/multi_blob
    library/cpp/on_disk/st_hash
    library/cpp/digest/crc32c
    library/cpp/digest/md5
    library/cpp/fieldcalc
    library/cpp/regex/pcre
    library/cpp/retry
    contrib/libs/sparsehash
)

# TODO: fix ymake internal parameter split so that `-s "TFileData,TCommandData"' would work
RUN_PROGRAM(
    tools/struct2fieldcalc -S -R ${ARCADIA_ROOT} -s TFileData -s TCommandData file_store.h cmd_store.h
    IN file_store.h
    IN cmd_store.h
    OUTPUT_INCLUDES devtools/ymake/symbols/file_store.h
    OUTPUT_INCLUDES devtools/ymake/symbols/cmd_store.h
    STDOUT dep_types.h_dumper.cpp
)

END()

IF (SANITIZER_TYPE OR YA_OPENSOURCE OR OPENSOURCE)
ELSE()
    RECURSE_FOR_TESTS(benchmark)
ENDIF()
