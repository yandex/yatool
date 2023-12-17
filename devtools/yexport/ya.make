LIBRARY()

SRCS(
    cmake_generator.cpp
    dir_cleaner.cpp
    export_file_manager.cpp
    generator_spec.cpp
    jinja_generator.cpp
    jinja_helpers.cpp
    path_hash.cpp
    py_requirements_generator.cpp
    read_sem_graph.cpp
    render_cmake.cpp
    sem_graph.cpp
    spec_based_generator.cpp
    target_replacements.cpp
    yexport_generator.cpp
    yexport_spec.cpp
)

RESOURCE(
    cmake/android.armv7.profile android.armv7.profile
    cmake/android.arm64.profile android.arm64.profile
    cmake/android.x86.profile android.x86.profile
    cmake/android.x86_64.profile android.x86_64.profile
    cmake/linux.aarch64.profile linux.aarch64.profile
    cmake/linux.ppc64le.profile linux.ppc64le.profile
    cmake/macos.arm64.profile macos.arm64.profile
)

PEERDIR(
    contrib/libs/fmt
    contrib/libs/jinja2cpp
    contrib/libs/toml11
    contrib/restricted/spdlog
    library/cpp/digest/md5
    library/cpp/json
    library/cpp/getopt/small
    devtools/yexport/diag
    devtools/ymake
    devtools/ymake/compact_graph
    devtools/ymake/common
    devtools/ymake/lang/makelists
)

GENERATE_ENUM_SERIALIZATION(generator_spec_enum.h)

END()

RECURSE(
    bin
    diag
    docs
    it
    ut
    scripts
)
