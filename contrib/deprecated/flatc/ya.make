PROGRAM(flatc)

LICENSE(Apache-2.0)

VERSION(2.0.0)

ORIGINAL_SOURCE(https://github.com/google/flatbuffers/archive/refs/tags/v2.0.0.tar.gz)

ADDINCL(
    contrib/deprecated/flatc/flatbuffers/grpc
    contrib/deprecated/flatc/flatbuffers/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

CFLAGS(
    -DFLATBUFFERS_LOCALE_INDEPENDENT=1
)

SRCDIR(contrib/deprecated/flatc/flatbuffers)

SRCS(
    grpc/src/compiler/cpp_generator.cc
    grpc/src/compiler/go_generator.cc
    grpc/src/compiler/java_generator.cc
    grpc/src/compiler/python_generator.cc
    grpc/src/compiler/swift_generator.cc
    grpc/src/compiler/ts_generator.cc
    src/code_generators.cpp
    src/flatc.cpp
    src/flatc_main.cpp
    src/idl_gen_cpp.cpp
    src/idl_gen_cpp_yandex_maps_iter.cpp
    src/idl_gen_csharp.cpp
    src/idl_gen_dart.cpp
    src/idl_gen_fbs.cpp
    src/idl_gen_go.cpp
    src/idl_gen_grpc.cpp
    src/idl_gen_java.cpp
    src/idl_gen_json_schema.cpp
    src/idl_gen_kotlin.cpp
    src/idl_gen_lobster.cpp
    src/idl_gen_lua.cpp
    src/idl_gen_php.cpp
    src/idl_gen_python.cpp
    src/idl_gen_rust.cpp
    src/idl_gen_swift.cpp
    src/idl_gen_text.cpp
    src/idl_gen_ts.cpp
    src/idl_parser.cpp
    src/reflection.cpp
    src/util.cpp
)

END()
