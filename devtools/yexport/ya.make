LIBRARY()

SRCS(
    cmake_generator.cpp
    dir_cleaner.cpp
    export_file_manager.cpp
    flat_attribute.cpp
    generator_spec.cpp
    graph_visitor.cpp
    jinja_generator.cpp
    jinja_helpers.cpp
    project.cpp
    py_requirements_generator.cpp
    read_sem_graph.cpp
    render_cmake.cpp
    sem_graph.cpp
    spec_based_generator.cpp
    std_helpers.cpp
    target_replacements.cpp
    yexport_generator.cpp
    yexport_spec.cpp
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
)
