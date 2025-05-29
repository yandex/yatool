LIBRARY()

SRCS(
    attribute.cpp
    attributes.cpp
    debug.cpp
    dir_cleaner.cpp
    dump.cpp
    export_file_manager.cpp
    generator_spec.cpp
    graph_visitor.cpp
    jinja_generator.cpp
    jinja_helpers.cpp
    jinja_template.cpp
    logging.cpp
    options.cpp
    project.cpp
    py_requirements_generator.cpp
    read_sem_graph.cpp
    sem_graph.cpp
    spec_based_generator.cpp
    stat.cpp
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
    docs/public
    it
    ut
)
