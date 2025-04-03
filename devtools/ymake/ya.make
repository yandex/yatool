LIBRARY()

USE_PYTHON3()

PEERDIR(
    contrib/libs/asio
    contrib/libs/yaml-cpp
    contrib/python/PyYAML
    contrib/python/python-rapidjson
    contrib/python/six
    contrib/libs/fmt
    contrib/libs/re2
    devtools/draft
    devtools/common/blacklist
    devtools/libs/yaplatform
    devtools/ymake/all_srcs
    devtools/ymake/common
    devtools/ymake/compact_graph
    devtools/ymake/diag
    devtools/ymake/include_parsers
    devtools/ymake/lang
    devtools/ymake/lang/makelists
    devtools/ymake/make_plan
    devtools/ymake/options
    devtools/ymake/resolver
    devtools/ymake/symbols
    devtools/ymake/yndex
    library/cpp/blockcodecs
    library/cpp/containers/absl_flat_hash
    library/cpp/containers/comptrie
    library/cpp/containers/top_keeper
    library/cpp/string_utils/levenshtein_diff
    library/cpp/digest/md5
    library/cpp/getopt/small
    library/cpp/iterator
    library/cpp/json
    library/cpp/json/writer
    library/cpp/on_disk/multi_blob
    library/cpp/pybind
    library/cpp/regex/pcre
    library/cpp/resource
    library/cpp/sighandler
    library/cpp/string_utils/base64
    library/cpp/svnversion
    library/cpp/ucompress
    library/cpp/zipatch
)

IF (MSVC)
    # default flags are good
ELSE()
    SET(RAGEL6_FLAGS -lF1)
ENDIF()

SRCS(
    action.cpp
    add_dep_adaptor.cpp
    add_iter_debug.cpp
    add_iter.cpp
    add_node_context.cpp
    addincls.cpp
    args_converter.cpp
    blacklist.cpp
    blacklist_checker.cpp
    cmd_properties.cpp
    command_helpers.cpp
    command_store.cpp
    commands/mods/common.cpp
    GLOBAL commands/mods/io.cpp
    GLOBAL commands/mods/paths.cpp
    GLOBAL commands/mods/strings.cpp
    GLOBAL commands/mods/structurals.cpp
    GLOBAL commands/mods/tagged_strings.cpp
    GLOBAL commands/mods/utility.cpp
    commands/compilation.cpp
    commands/evaluation.cpp
    commands/mod_registry.cpp
    commands/preeval.cpp
    commands/preeval_reducer.cpp
    commands/script_evaluator.cpp
    compute_reachability.cpp
    conf.cpp
    config/config.cpp
    configure_tasks.cpp
    dump_graph_info.cpp
    debug_log.cpp
    dependency_management.cpp
    diag_reporter.cpp
    dirs.cpp
    dump_owners.cpp
    exec.cpp
    evlog_server.cpp
    export_json_debug.cpp
    export_json.cpp
    flat_json_graph.cpp
    general_parser.cpp
    global_vars_collector.cpp
    graph_changes_predictor.cpp
    incl_fixer.cpp
    include_processors/base.cpp
    include_processors/cfgproto_processor.cpp
    include_processors/cpp_processor.cpp
    include_processors/cython_processor.cpp
    include_processors/flatc_processor.cpp
    include_processors/fortran_processor.cpp
    include_processors/go_processor.cpp
    include_processors/gzt_processor.cpp
    include_processors/include.cpp
    include_processors/lex_processor.cpp
    include_processors/mapkit_idl_processor.cpp
    include_processors/nlg_processor.cpp
    include_processors/parsers_cache.cpp
    include_processors/proto_processor.cpp
    include_processors/ragel_processor.cpp
    include_processors/ros_processor.cpp
    include_processors/swig_processor.cpp
    include_processors/ts_processor.cpp
    include_processors/xs_processor.cpp
    include_processors/ydl_processor.cpp
    induced_props_debug.cpp
    induced_props.cpp
    isolated_projects.cpp
    json_entry_stats.cpp
    json_md5.cpp
    json_subst.cpp
    json_visitor.cpp
    lang/plugin_facade.cpp # XXX
    licenses_conf.cpp
    autoincludes_conf.cpp
    list_modules.cpp
    macro_processor.cpp
    macro_string.cpp
    macro.cpp
    main.cpp
    make_plan_cache.cpp
    makefile_loader.cpp
    managed_deps_iter.cpp
    md5_debug.cpp
    mine_variables.cpp
    mkcmd.cpp
    module_add_data.cpp
    module_builder.cpp
    module_confs.cpp
    module_dir.cpp
    module_loader.cpp
    module_resolver.cpp
    module_restorer.cpp
    module_state.cpp
    module_store.cpp
    module_wrapper.cpp
    node_builder.cpp
    node_printer.cpp
    out.cpp
    parser_manager.cpp
    path_matcher.cpp
    peers_rules.cpp
    peers.cpp
    plugins/cpp_plugins.cpp
    plugins/error.cpp
    plugins/init.cpp
    plugins/ymake_module.cpp
    plugins/plugin_go_fake_output_handler.cpp
    plugins/plugin_macro_impl.cpp
    plugins/resource_handler/impl.cpp
    plugins/scoped_py_object_ptr.cpp
    plugins/ymake_module_adapter.cpp
    propagate_change_flags.cpp
    recurse_graph.cpp
    run_main.cpp
    saveload.cpp
    sem_graph.cpp
    shell_subst.cpp
    spdx.cpp
    sysincl_conf.cpp
    sysincl_resolver.cpp
    tools_miner.cpp
    trace_start.cpp
    transitive_constraints.cpp
    transitive_requirements_check.cpp
    transitive_state.cpp
    vars.cpp
    ymake.cpp
)

IF (NOT OPENSOURCE)
    PEERDIR(
        library/cpp/xml/document
    )
ENDIF()

IF (MSVC)
    CFLAGS(
        GLOBAL -DASIO_WINDOWS_APP
    )
ENDIF()

GENERATE_ENUM_SERIALIZATION(add_iter_debug.h)
GENERATE_ENUM_SERIALIZATION(config/config.h)
GENERATE_ENUM_SERIALIZATION(config/transition.h)
GENERATE_ENUM_SERIALIZATION(export_json_debug.h)
GENERATE_ENUM_SERIALIZATION(include_processors/include.h)
GENERATE_ENUM_SERIALIZATION(induced_props.h)
GENERATE_ENUM_SERIALIZATION(module_resolver.h)
GENERATE_ENUM_SERIALIZATION(module_state.h)
GENERATE_ENUM_SERIALIZATION(peers_rules.h)

PY_REGISTER(ymake)

END()

RECURSE(
    bin
    common
    compact_graph
    diag
    lang
    lang/expansion_fuzz
    lang/confreader_fuzz
    options
    polexpr
    resolver
    stub
    symbols
    tools
    yndex
)

RECURSE_FOR_TESTS(
    common/ut
    compact_graph/perf
    compact_graph/ut
    include_parsers/ut
    include_processors/ut
    lang/makelists/fuzz
    lang/makelists/ut
    resolver/ut
    symbols/ut
    ut
    ut/hexencoder
)

IF (OS_WINDOWS OR YA_OPENSOURCE OR OPENSOURCE)
    # This excludes integrational tests, but leaves unit tests
    # For OpenSource some tests will be re-added back, but definitely not all
ELSE()
    RECURSE(
        sandbox
        tests
    )
ENDIF()
