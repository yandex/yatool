LIBRARY()

SRCS(
    cmake_generator.cpp
    dir_cleaner.cpp
    export_file_manager.cpp
    generator_spec.cpp
    jinja_generator.cpp
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
    cmake/archive.cmake archive.cmake
    cmake/conan.cmake conan.cmake
    cmake/common.cmake common.cmake
    cmake/protobuf.cmake protobuf.cmake
    cmake/antlr.cmake antlr.cmake
    cmake/bison.cmake bison.cmake
    cmake/llvm-tools.cmake llvm-tools.cmake
    cmake/masm.cmake masm.cmake
    cmake/fat_object.cmake fat_object.cmake
    cmake/recursive_library.cmake recursive_library.cmake
    cmake/shared_libs.cmake shared_libs.cmake
    cmake/cuda.cmake cuda.cmake
    cmake/cython.cmake cython.cmake
    cmake/fbs.cmake fbs.cmake
    cmake/swig.cmake swig.cmake
    cmake/gather_swig_java.cmake gather_swig_java.cmake
    cmake/global_flags.cmake global_flags.cmake
    cmake/global_flags.compiler.gnu.cmake global_flags.compiler.gnu.cmake
    cmake/global_flags.compiler.msvc.cmake global_flags.compiler.msvc.cmake
    cmake/global_flags.linker.gnu.cmake global_flags.linker.gnu.cmake
    cmake/global_flags.linker.msvc.cmake global_flags.linker.msvc.cmake
    cmake/FindAIO.cmake FindAIO.cmake
    cmake/FindIDN.cmake FindIDN.cmake
    cmake/FindJNITarget.cmake FindJNITarget.cmake
    cmake/android.armv7.profile android.armv7.profile
    cmake/android.arm64.profile android.arm64.profile
    cmake/android.x86.profile android.x86.profile
    cmake/android.x86_64.profile android.x86_64.profile
    cmake/linux.aarch64.profile linux.aarch64.profile
    cmake/linux.ppc64le.profile linux.ppc64le.profile
    cmake/macos.arm64.profile macos.arm64.profile
    scripts/create_recursive_library_for_cmake.py create_recursive_library_for_cmake.py
    scripts/export_script_gen.py export_script_gen.py
    scripts/split_unittest.py split_unittest.py
    scripts/generate_vcs_info.py generate_vcs_info.py
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

GENERATE_ENUM_SERIALIZATION_WITH_HEADER(known_modules.h)

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
