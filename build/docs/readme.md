*Do not edit, this file is generated from comments to macros definitions using `ya dump conf-docs`.*

# ya.make commands

General info: [How to write ya.make files](https://docs.yandex-team.ru/ya-make/manual/)

## Table of contents

   * [Multimodules](#multimodules)
       - Multimodule [DLL_JAVA](#multimodule_DLL_JAVA)
       - Multimodule [DOCS](#multimodule_DOCS)
       - Multimodule [FBS_LIBRARY](#multimodule_FBS_LIBRARY)
       - Multimodule [JAVA_ANNOTATION_PROCESSOR](#multimodule_JAVA_ANNOTATION_PROCESSOR)
       - Multimodule [JAVA_CONTRIB_ANNOTATION_PROCESSOR](#multimodule_JAVA_CONTRIB_ANNOTATION_PROCESSOR)
       - Multimodule [JAVA_CONTRIB_PROGRAM](#multimodule_JAVA_CONTRIB_PROGRAM)
       - Multimodule [JAVA_LIBRARY_SPLIT](#multimodule_JAVA_LIBRARY_SPLIT)
       - Multimodule [JAVA_PROGRAM](#multimodule_JAVA_PROGRAM)
       - Multimodule [JTEST](#multimodule_JTEST)
       - Multimodule [JTEST_FOR](#multimodule_JTEST_FOR)
       - Multimodule [JUNIT5](#multimodule_JUNIT5)
       - Multimodule [JUNIT6](#multimodule_JUNIT6)
       - Multimodule [PACKAGE](#multimodule_PACKAGE)
       - Multimodule [PROTO_LIBRARY](#multimodule_PROTO_LIBRARY)
       - Multimodule [PROTO_SCHEMA](#multimodule_PROTO_SCHEMA)
       - Multimodule [PY23_LIBRARY](#multimodule_PY23_LIBRARY)
       - Multimodule [PY23_NATIVE_LIBRARY](#multimodule_PY23_NATIVE_LIBRARY)
       - Multimodule [PY23_TEST](#multimodule_PY23_TEST)
       - Multimodule [PY3TEST](#multimodule_PY3TEST)
       - Multimodule [PY3_PROGRAM](#multimodule_PY3_PROGRAM)
       - Multimodule [TS_LIBRARY](#multimodule_TS_LIBRARY)
       - Multimodule [TS_NEXT](#multimodule_TS_NEXT)
       - Multimodule [TS_PACKAGE](#multimodule_TS_PACKAGE)
       - Multimodule [TS_RSPACK](#multimodule_TS_RSPACK)
       - Multimodule [TS_TEST_FOR](#multimodule_TS_TEST_FOR)
       - Multimodule [TS_TSC](#multimodule_TS_TSC)
       - Multimodule [TS_VITE](#multimodule_TS_VITE)
       - Multimodule [TS_WEBPACK](#multimodule_TS_WEBPACK)
       - Multimodule [YQL_UDF](#multimodule_YQL_UDF)
       - Multimodule [YQL_UDF_CONTRIB](#multimodule_YQL_UDF_CONTRIB)
       - Multimodule [YQL_UDF_YDB](#multimodule_YQL_UDF_YDB)
   * [Modules](#modules)
       - Module [BOOSTTEST](#module_BOOSTTEST)
       - Module [BOOSTTEST_WITH_MAIN](#module_BOOSTTEST_WITH_MAIN)
       - Module [CI_GROUP](#module_CI_GROUP)
       - Module [CUDA_DEVICE_LINK_LIBRARY](#module_CUDA_DEVICE_LINK_LIBRARY)
       - Module [DEFAULT_IOS_INTERFACE](#module_DEFAULT_IOS_INTERFACE)
       - Module [DLL](#module_DLL)
       - Module [DLL_TOOL](#module_DLL_TOOL)
       - Module [DOCS_HTML](#module_DOCS_HTML)
       - Module [DOCS_LIBRARY](#module_DOCS_LIBRARY)
       - Module [EXECTEST](#module_EXECTEST)
       - Module [FAT_OBJECT](#module_FAT_OBJECT)
       - Module [FUZZ](#module_FUZZ)
       - Module [GEN_LIBRARY](#module_GEN_LIBRARY)
       - Module [GO_DLL](#module_GO_DLL)
       - Module [GO_LIBRARY](#module_GO_LIBRARY)
       - Module [GO_PROGRAM](#module_GO_PROGRAM)
       - Module [GO_TEST](#module_GO_TEST)
       - Module [GTEST](#module_GTEST)
       - Module [G_BENCHMARK](#module_G_BENCHMARK)
       - Module [IOS_INTERFACE](#module_IOS_INTERFACE)
       - Module [JAVA_CONTRIB](#module_JAVA_CONTRIB)
       - Module [JAVA_CONTRIB_PROXY](#module_JAVA_CONTRIB_PROXY)
       - Module [JAVA_LIBRARY](#module_JAVA_LIBRARY)
       - Module [JAVA_TEST_LIBRARY](#module_JAVA_TEST_LIBRARY)
       - Module [LIBRARY](#module_LIBRARY)
       - Module [PROGRAM](#module_PROGRAM)
       - Module [PROTO_DESCRIPTIONS](#module_PROTO_DESCRIPTIONS)
       - Module [PROTO_REGISTRY](#module_PROTO_REGISTRY)
       - Module [PY2MODULE](#module_PY2MODULE)
       - Module [PY2TEST](#module_PY2TEST)
       - Module [PY2_LIBRARY](#module_PY2_LIBRARY)
       - Module [PY2_PROGRAM](#module_PY2_PROGRAM)
       - Module [PY3MODULE](#module_PY3MODULE)
       - Module [PY3TEST_BIN](#module_PY3TEST_BIN)
       - Module [PY3_LIBRARY](#module_PY3_LIBRARY)
       - Module [PY3_PROGRAM_BIN](#module_PY3_PROGRAM_BIN)
       - Module [PYTEST_BIN](#module_PYTEST_BIN)
       - Module [PY_ANY_MODULE](#module_PY_ANY_MODULE)
       - Module [RECURSIVE_LIBRARY](#module_RECURSIVE_LIBRARY)
       - Module [RESOURCES_LIBRARY](#module_RESOURCES_LIBRARY)
       - Module [R_MODULE](#module_R_MODULE)
       - Module [SO_PROGRAM](#module_SO_PROGRAM)
       - Module [TS_TEST_HERMIONE_FOR](#module_TS_TEST_HERMIONE_FOR)
       - Module [TS_TEST_JEST_FOR](#module_TS_TEST_JEST_FOR)
       - Module [TS_TEST_PLAYWRIGHT_FOR](#module_TS_TEST_PLAYWRIGHT_FOR)
       - Module [TS_TEST_PLAYWRIGHT_LARGE_FOR](#module_TS_TEST_PLAYWRIGHT_LARGE_FOR)
       - Module [TS_TEST_VITEST_FOR](#module_TS_TEST_VITEST_FOR)
       - Module [UNION](#module_UNION)
       - Module [UNITTEST](#module_UNITTEST)
       - Module [UNITTEST_FOR](#module_UNITTEST_FOR)
       - Module [UNITTEST_WITH_CUSTOM_ENTRY_POINT](#module_UNITTEST_WITH_CUSTOM_ENTRY_POINT)
       - Module [YQL_PYTHON3_UDF](#module_YQL_PYTHON3_UDF)
       - Module [YQL_PYTHON3_UDF_TEST](#module_YQL_PYTHON3_UDF_TEST)
       - Module [YQL_PYTHON_UDF](#module_YQL_PYTHON_UDF)
       - Module [YQL_PYTHON_UDF_PROGRAM](#module_YQL_PYTHON_UDF_PROGRAM)
       - Module [YQL_PYTHON_UDF_TEST](#module_YQL_PYTHON_UDF_TEST)
       - Module [YQL_UDF_MINITEST](#module_YQL_UDF_MINITEST)
       - Module [YQL_UDF_MODULE](#module_YQL_UDF_MODULE)
       - Module [YQL_UDF_MODULE_CONTRIB](#module_YQL_UDF_MODULE_CONTRIB)
       - Module [YQL_UDF_TEST](#module_YQL_UDF_TEST)
       - Module [YQL_UDF_YDB_MODULE](#module_YQL_UDF_YDB_MODULE)
       - Module [YT_UNITTEST](#module_YT_UNITTEST)
       - Module [Y_BENCHMARK](#module_Y_BENCHMARK)
   * [Macros](#macros)

     <details><summary><b>A</b> &nbsp; <i>(31 macros)</i></summary>

     - Macro [ACCELEO](#macro_ACCELEO)
     - Macro [ADDINCL](#macro_ADDINCL)
     - Macro [ADDINCLSELF](#macro_ADDINCLSELF)
     - Macro [ADD_CHECK](#macro_ADD_CHECK)
     - Macro [ADD_CHECK_PY_IMPORTS](#macro_ADD_CHECK_PY_IMPORTS)
     - Macro [ADD_CLANG_TIDY](#macro_ADD_CLANG_TIDY)
     - Macro [ADD_COMPILABLE_TRANSLATE](#macro_ADD_COMPILABLE_TRANSLATE)
     - Macro [ADD_COMPILABLE_TRANSLIT](#macro_ADD_COMPILABLE_TRANSLIT)
     - Macro [ADD_DLLS_TO_JAR](#macro_ADD_DLLS_TO_JAR)
     - Macro [ADD_IWYU](#macro_ADD_IWYU)
     - Macro [ADD_PYTEST_BIN](#macro_ADD_PYTEST_BIN)
     - Macro [ADD_YTEST](#macro_ADD_YTEST)
     - Macro [ALICE_GENERATE_FUNCTION_PROTO_INCLUDES](#macro_ALICE_GENERATE_FUNCTION_PROTO_INCLUDES)
     - Macro [ALICE_GENERATE_FUNCTION_SPECS](#macro_ALICE_GENERATE_FUNCTION_SPECS)
     - Macro [ALLOCATOR](#macro_ALLOCATOR)
     - Macro [ALLOCATOR_IMPL](#macro_ALLOCATOR_IMPL)
     - Macro [ALL_GO_SRCS](#macro_ALL_GO_SRCS)
     - Macro [ALL_PYTEST_SRCS](#macro_ALL_PYTEST_SRCS)
     - Macro [ALL_PY_EXTRA_LINT_FILES](#macro_ALL_PY_EXTRA_LINT_FILES)
     - Macro [ALL_PY_SRCS](#macro_ALL_PY_SRCS)
     - Macro [ALL_RESOURCE_FILES](#macro_ALL_RESOURCE_FILES)
     - Macro [ALL_RESOURCE_FILES_FROM_DIRS](#macro_ALL_RESOURCE_FILES_FROM_DIRS)
     - Macro [ALL_SRCS](#macro_ALL_SRCS)
     - Macro [ANNOTATION_PROCESSOR](#macro_ANNOTATION_PROCESSOR)
     - Macro [ARCHIVE](#macro_ARCHIVE)
     - Macro [ARCHIVE_ASM](#macro_ARCHIVE_ASM)
     - Macro [ARCHIVE_BY_KEYS](#macro_ARCHIVE_BY_KEYS)
     - Macro [AR_PLUGIN](#macro_AR_PLUGIN)
     - Macro [ASM_PREINCLUDE](#macro_ASM_PREINCLUDE)
     - Macro [ASSERT](#macro_ASSERT)
     - Macro [AUTO_SERVICE](#macro_AUTO_SERVICE)

     </details>

     <details><summary><b>B</b> &nbsp; <i>(16 macros)</i></summary>

     - Macro [BENCHMARK_OPTS](#macro_BENCHMARK_OPTS)
     - Macro [BISON_FLAGS](#macro_BISON_FLAGS)
     - Macro [BISON_GEN_C](#macro_BISON_GEN_C)
     - Macro [BISON_GEN_CPP](#macro_BISON_GEN_CPP)
     - Macro [BISON_HEADER](#macro_BISON_HEADER)
     - Macro [BISON_NO_HEADER](#macro_BISON_NO_HEADER)
     - Macro [BPF](#macro_BPF)
     - Macro [BPF_STATIC](#macro_BPF_STATIC)
     - Macro [BUILDWITH_CYTHON_C](#macro_BUILDWITH_CYTHON_C)
     - Macro [BUILDWITH_CYTHON_CPP](#macro_BUILDWITH_CYTHON_CPP)
     - Macro [BUILDWITH_RAGEL6](#macro_BUILDWITH_RAGEL6)
     - Macro [BUILD_CATBOOST](#macro_BUILD_CATBOOST)
     - Macro [BUILD_ONLY_IF](#macro_BUILD_ONLY_IF)
     - Macro [BUILD_YDL_DESC](#macro_BUILD_YDL_DESC)
     - Macro [BUNDLE](#macro_BUNDLE)
     - Macro [BUNDLE_OUTPUT](#macro_BUNDLE_OUTPUT)

     </details>

     <details><summary><b>C</b> &nbsp; <i>(48 macros)</i></summary>

     - Macro [CFLAGS](#macro_CFLAGS)
     - Macro [CGO_CFLAGS](#macro_CGO_CFLAGS)
     - Macro [CGO_LDFLAGS](#macro_CGO_LDFLAGS)
     - Macro [CGO_SRCS](#macro_CGO_SRCS)
     - Macro [CHECK_ALLOWED_PATH](#macro_CHECK_ALLOWED_PATH)
     - Macro [CHECK_CONTRIB_CREDITS](#macro_CHECK_CONTRIB_CREDITS)
     - Macro [CHECK_DEPENDENT_DIRS](#macro_CHECK_DEPENDENT_DIRS)
     - Macro [CHECK_JAVA_DEPS](#macro_CHECK_JAVA_DEPS)
     - Macro [CLANG_EMIT_AST_CXX](#macro_CLANG_EMIT_AST_CXX)
     - Macro [CLANG_EMIT_AST_CXX_RUN_TOOL](#macro_CLANG_EMIT_AST_CXX_RUN_TOOL)
     - Macro [CLANG_WARNINGS](#macro_CLANG_WARNINGS)
     - Macro [CLEAN_TEXTREL](#macro_CLEAN_TEXTREL)
     - Macro [CMAKE_EXPORTED_TARGET_NAME](#macro_CMAKE_EXPORTED_TARGET_NAME)
     - Macro [COLLECT_CONFIG_FILES](#macro_COLLECT_CONFIG_FILES)
     - Macro [COLLECT_FRONTEND_FILES](#macro_COLLECT_FRONTEND_FILES)
     - Macro [COLLECT_GO_SWAGGER_FILES](#macro_COLLECT_GO_SWAGGER_FILES)
     - Macro [COLLECT_JINJA_TEMPLATES](#macro_COLLECT_JINJA_TEMPLATES)
     - Macro [COLLECT_KOTLIN_LINT_SRCS](#macro_COLLECT_KOTLIN_LINT_SRCS)
     - Macro [COLLECT_YAML_CONFIG_FILES](#macro_COLLECT_YAML_CONFIG_FILES)
     - Macro [COMPILE_C_AS_CXX](#macro_COMPILE_C_AS_CXX)
     - Macro [COMPILE_LUA](#macro_COMPILE_LUA)
     - Macro [COMPILE_LUA_21](#macro_COMPILE_LUA_21)
     - Macro [COMPILE_LUA_OPENRESTY](#macro_COMPILE_LUA_OPENRESTY)
     - Macro [CONFIGURE_FILE](#macro_CONFIGURE_FILE)
     - Macro [CONFTEST_LOAD_POLICY_LEGACY_GLOBAL](#macro_CONFTEST_LOAD_POLICY_LEGACY_GLOBAL)
     - Macro [CONFTEST_LOAD_POLICY_LOCAL](#macro_CONFTEST_LOAD_POLICY_LOCAL)
     - Macro [CONLYFLAGS](#macro_CONLYFLAGS)
     - Macro [COPY](#macro_COPY)
     - Macro [COPY_FILE](#macro_COPY_FILE)
     - Macro [COPY_FILE_WITH_CONTEXT](#macro_COPY_FILE_WITH_CONTEXT)
     - Macro [COW](#macro_COW)
     - Macro [CPP_ADDINCL](#macro_CPP_ADDINCL)
     - Macro [CPP_ENUMS_SERIALIZATION](#macro_CPP_ENUMS_SERIALIZATION)
     - Macro [CPP_EVLOG](#macro_CPP_EVLOG)
     - Macro [CPP_EV_PROTO_PLUGIN](#macro_CPP_EV_PROTO_PLUGIN)
     - Macro [CPP_PROTOLIBS_DEBUG_INFO](#macro_CPP_PROTOLIBS_DEBUG_INFO)
     - Macro [CPP_PROTO_PLUGIN](#macro_CPP_PROTO_PLUGIN)
     - Macro [CPP_PROTO_PLUGIN0](#macro_CPP_PROTO_PLUGIN0)
     - Macro [CPP_PROTO_PLUGIN2](#macro_CPP_PROTO_PLUGIN2)
     - Macro [CREATE_BUILDINFO_FOR](#macro_CREATE_BUILDINFO_FOR)
     - Macro [CREATE_INIT_PY_STRUCTURE](#macro_CREATE_INIT_PY_STRUCTURE)
     - Macro [CREDITS_DISCLAIMER](#macro_CREDITS_DISCLAIMER)
     - Macro [CTEMPLATE_VARNAMES](#macro_CTEMPLATE_VARNAMES)
     - Macro [CUDA_NVCC_FLAGS](#macro_CUDA_NVCC_FLAGS)
     - Macro [CUDA_SRCS](#macro_CUDA_SRCS)
     - Macro [CUSTOM_LINK_STEP_SCRIPT](#macro_CUSTOM_LINK_STEP_SCRIPT)
     - Macro [CXXFLAGS](#macro_CXXFLAGS)
     - Macro [CYTHON_FLAGS](#macro_CYTHON_FLAGS)

     </details>

     <details><summary><b>D</b> &nbsp; <i>(30 macros)</i></summary>

     - Macro [DARWIN_SIGNED_RESOURCE](#macro_DARWIN_SIGNED_RESOURCE)
     - Macro [DARWIN_STRINGS_RESOURCE](#macro_DARWIN_STRINGS_RESOURCE)
     - Macro [DATA](#macro_DATA)
     - Macro [DATA_FILES](#macro_DATA_FILES)
     - Macro [DEB_VERSION](#macro_DEB_VERSION)
     - Macro [DECIMAL_MD5_LOWER_32_BITS](#macro_DECIMAL_MD5_LOWER_32_BITS)
     - Macro [DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE](#macro_DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE)
     - Macro [DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON](#macro_DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON)
     - Macro [DECLARE_EXTERNAL_HOST_RESOURCES_PACK](#macro_DECLARE_EXTERNAL_HOST_RESOURCES_PACK)
     - Macro [DECLARE_EXTERNAL_RESOURCE](#macro_DECLARE_EXTERNAL_RESOURCE)
     - Macro [DECLARE_EXTERNAL_RESOURCE_BY_JSON](#macro_DECLARE_EXTERNAL_RESOURCE_BY_JSON)
     - Macro [DECLARE_IN_DIRS](#macro_DECLARE_IN_DIRS)
     - Macro [DEFAULT](#macro_DEFAULT)
     - Macro [DEFAULT_JAVA_SRCS_LAYOUT](#macro_DEFAULT_JAVA_SRCS_LAYOUT)
     - Macro [DEFAULT_JDK_VERSION](#macro_DEFAULT_JDK_VERSION)
     - Macro [DEFAULT_JUNIT_JAVA_SRCS_LAYOUT](#macro_DEFAULT_JUNIT_JAVA_SRCS_LAYOUT)
     - Macro [DEPENDENCY_MANAGEMENT](#macro_DEPENDENCY_MANAGEMENT)
     - Macro [DEPENDS](#macro_DEPENDS)
     - Macro [DIRECT_DEPS_ONLY](#macro_DIRECT_DEPS_ONLY)
     - Macro [DISABLE](#macro_DISABLE)
     - Macro [DISABLE_DATA_VALIDATION](#macro_DISABLE_DATA_VALIDATION)
     - Macro [DLL_FOR](#macro_DLL_FOR)
     - Macro [DOCKER_IMAGE](#macro_DOCKER_IMAGE)
     - Macro [DOCS_CONFIG](#macro_DOCS_CONFIG)
     - Macro [DOCS_COPY_FILES](#macro_DOCS_COPY_FILES)
     - Macro [DOCS_DIR](#macro_DOCS_DIR)
     - Macro [DOCS_HTML_FROM](#macro_DOCS_HTML_FROM)
     - Macro [DOCS_INCLUDE_SOURCES](#macro_DOCS_INCLUDE_SOURCES)
     - Macro [DOCS_VARS](#macro_DOCS_VARS)
     - Macro [DYNAMIC_LIBRARY_FROM](#macro_DYNAMIC_LIBRARY_FROM)

     </details>

     <details><summary><b>E</b> &nbsp; <i>(19 macros)</i></summary>

     - Macro [ELSE](#macro_ELSE)
     - Macro [ELSEIF](#macro_ELSEIF)
     - Macro [EMBED_JAVA_VCS_INFO](#macro_EMBED_JAVA_VCS_INFO)
     - Macro [ENABLE](#macro_ENABLE)
     - Macro [ENABLE_PREVIEW](#macro_ENABLE_PREVIEW)
     - Macro [END](#macro_END)
     - Macro [ENDIF](#macro_ENDIF)
     - Macro [ENV](#macro_ENV)
     - Macro [EVLOG_CMD](#macro_EVLOG_CMD)
     - Macro [EXCLUDE](#macro_EXCLUDE)
     - Macro [EXCLUDE_TAGS](#macro_EXCLUDE_TAGS)
     - Macro [EXPERIMENTAL_FORK](#macro_EXPERIMENTAL_FORK)
     - Macro [EXPLICIT_DATA](#macro_EXPLICIT_DATA)
     - Macro [EXPLICIT_OUTPUTS](#macro_EXPLICIT_OUTPUTS)
     - Macro [EXPORTS_SCRIPT](#macro_EXPORTS_SCRIPT)
     - Macro [EXPORT_ALL_DYNAMIC_SYMBOLS](#macro_EXPORT_ALL_DYNAMIC_SYMBOLS)
     - Macro [EXTERNAL_RESOURCE](#macro_EXTERNAL_RESOURCE)
     - Macro [EXTRADIR](#macro_EXTRADIR)
     - Macro [EXTRALIBS_STATIC](#macro_EXTRALIBS_STATIC)

     </details>

     <details><summary><b>F</b> &nbsp; <i>(18 macros)</i></summary>

     - Macro [FBS_CMD](#macro_FBS_CMD)
     - Macro [FBS_NAMESPACE](#macro_FBS_NAMESPACE)
     - Macro [FBS_TO_PY2SRC](#macro_FBS_TO_PY2SRC)
     - Macro [FILES](#macro_FILES)
     - Macro [FLATC_FLAGS](#macro_FLATC_FLAGS)
     - Macro [FLAT_JOIN_SRCS_GLOBAL](#macro_FLAT_JOIN_SRCS_GLOBAL)
     - Macro [FLEX_FLAGS](#macro_FLEX_FLAGS)
     - Macro [FLEX_GEN_C](#macro_FLEX_GEN_C)
     - Macro [FLEX_GEN_CPP](#macro_FLEX_GEN_CPP)
     - Macro [FORK_SUBTESTS](#macro_FORK_SUBTESTS)
     - Macro [FORK_TESTS](#macro_FORK_TESTS)
     - Macro [FORK_TEST_FILES](#macro_FORK_TEST_FILES)
     - Macro [FROM_ARCHIVE](#macro_FROM_ARCHIVE)
     - Macro [FROM_SANDBOX](#macro_FROM_SANDBOX)
     - Macro [FULL_JAVA_SRCS](#macro_FULL_JAVA_SRCS)
     - Macro [FUNCTION_ORDERING_FILE](#macro_FUNCTION_ORDERING_FILE)
     - Macro [FUZZ_DICTS](#macro_FUZZ_DICTS)
     - Macro [FUZZ_OPTS](#macro_FUZZ_OPTS)

     </details>

     <details><summary><b>G</b> &nbsp; <i>(52 macros)</i></summary>

     - Macro [GENERATE_ENUM_SERIALIZATION](#macro_GENERATE_ENUM_SERIALIZATION)
     - Macro [GENERATE_ENUM_SERIALIZATION_WITH_HEADER](#macro_GENERATE_ENUM_SERIALIZATION_WITH_HEADER)
     - Macro [GENERATE_IMPLIB](#macro_GENERATE_IMPLIB)
     - Macro [GENERATE_PY_PROTOS](#macro_GENERATE_PY_PROTOS)
     - Macro [GENERATE_SCRIPT](#macro_GENERATE_SCRIPT)
     - Macro [GENERATE_YT_RECORD](#macro_GENERATE_YT_RECORD)
     - Macro [GEN_SCHEEME2](#macro_GEN_SCHEEME2)
     - Macro [GLOBAL_CFLAGS](#macro_GLOBAL_CFLAGS)
     - Macro [GLOBAL_SRCS](#macro_GLOBAL_SRCS)
     - Macro [GOLANG_VERSION](#macro_GOLANG_VERSION)
     - Macro [GO_ASM_FLAGS](#macro_GO_ASM_FLAGS)
     - Macro [GO_BENCH_TIMEOUT](#macro_GO_BENCH_TIMEOUT)
     - Macro [GO_CGO1_FLAGS](#macro_GO_CGO1_FLAGS)
     - Macro [GO_CGO2_FLAGS](#macro_GO_CGO2_FLAGS)
     - Macro [GO_COMPILE_FLAGS](#macro_GO_COMPILE_FLAGS)
     - Macro [GO_EMBED_BINDIR](#macro_GO_EMBED_BINDIR)
     - Macro [GO_EMBED_DIR](#macro_GO_EMBED_DIR)
     - Macro [GO_EMBED_PATTERN](#macro_GO_EMBED_PATTERN)
     - Macro [GO_EMBED_TEST_DIR](#macro_GO_EMBED_TEST_DIR)
     - Macro [GO_EMBED_XTEST_DIR](#macro_GO_EMBED_XTEST_DIR)
     - Macro [GO_GRPC_GATEWAY_SRCS](#macro_GO_GRPC_GATEWAY_SRCS)
     - Macro [GO_GRPC_GATEWAY_SWAGGER_SRCS](#macro_GO_GRPC_GATEWAY_SWAGGER_SRCS)
     - Macro [GO_GRPC_GATEWAY_V2_OPENAPI_SRCS](#macro_GO_GRPC_GATEWAY_V2_OPENAPI_SRCS)
     - Macro [GO_GRPC_GATEWAY_V2_SRCS](#macro_GO_GRPC_GATEWAY_V2_SRCS)
     - Macro [GO_LDFLAGS](#macro_GO_LDFLAGS)
     - Macro [GO_LINK_FLAGS](#macro_GO_LINK_FLAGS)
     - Macro [GO_MOCKGEN_CONTRIB_FROM](#macro_GO_MOCKGEN_CONTRIB_FROM)
     - Macro [GO_MOCKGEN_FROM](#macro_GO_MOCKGEN_FROM)
     - Macro [GO_MOCKGEN_MOCKS](#macro_GO_MOCKGEN_MOCKS)
     - Macro [GO_MOCKGEN_PACKAGE](#macro_GO_MOCKGEN_PACKAGE)
     - Macro [GO_MOCKGEN_REFLECT](#macro_GO_MOCKGEN_REFLECT)
     - Macro [GO_MOCKGEN_SOURCE](#macro_GO_MOCKGEN_SOURCE)
     - Macro [GO_MOCKGEN_TYPES](#macro_GO_MOCKGEN_TYPES)
     - Macro [GO_OAPI_CODEGEN](#macro_GO_OAPI_CODEGEN)
     - Macro [GO_OAPI_CODEGEN_TAXI](#macro_GO_OAPI_CODEGEN_TAXI)
     - Macro [GO_OAPI_CODEGEN_TAXI_1134](#macro_GO_OAPI_CODEGEN_TAXI_1134)
     - Macro [GO_OAPI_CODEGEN_V2](#macro_GO_OAPI_CODEGEN_V2)
     - Macro [GO_PACKAGE_NAME](#macro_GO_PACKAGE_NAME)
     - Macro [GO_PROTO_PLUGIN](#macro_GO_PROTO_PLUGIN)
     - Macro [GO_PROTO_USE_V2](#macro_GO_PROTO_USE_V2)
     - Macro [GO_SKIP_TESTS](#macro_GO_SKIP_TESTS)
     - Macro [GO_SSO](#macro_GO_SSO)
     - Macro [GO_SSO_TOOL](#macro_GO_SSO_TOOL)
     - Macro [GO_TEST_EMBED_BINDIR](#macro_GO_TEST_EMBED_BINDIR)
     - Macro [GO_TEST_EMBED_PATTERN](#macro_GO_TEST_EMBED_PATTERN)
     - Macro [GO_TEST_FOR](#macro_GO_TEST_FOR)
     - Macro [GO_TEST_SRCS](#macro_GO_TEST_SRCS)
     - Macro [GO_XTEST_EMBED_BINDIR](#macro_GO_XTEST_EMBED_BINDIR)
     - Macro [GO_XTEST_EMBED_PATTERN](#macro_GO_XTEST_EMBED_PATTERN)
     - Macro [GO_XTEST_SRCS](#macro_GO_XTEST_SRCS)
     - Macro [GRPC](#macro_GRPC)
     - Macro [GRPC_WITH_GMOCK](#macro_GRPC_WITH_GMOCK)

     </details>

     <details><summary><b>H</b> &nbsp; <i>(1 macro)</i></summary>

     - Macro [HEADERS](#macro_HEADERS)

     </details>

     <details><summary><b>I</b> &nbsp; <i>(14 macros)</i></summary>

     - Macro [IDEA_EXCLUDE_DIRS](#macro_IDEA_EXCLUDE_DIRS)
     - Macro [IDEA_MODULE_NAME](#macro_IDEA_MODULE_NAME)
     - Macro [IDEA_RESOURCE_DIRS](#macro_IDEA_RESOURCE_DIRS)
     - Macro [IF](#macro_IF)
     - Macro [INCLUDE](#macro_INCLUDE)
     - Macro [INCLUDE_ONCE](#macro_INCLUDE_ONCE)
     - Macro [INCLUDE_TAGS](#macro_INCLUDE_TAGS)
     - Macro [INDUCED_DEPS](#macro_INDUCED_DEPS)
     - Macro [INJECT_PEERS](#macro_INJECT_PEERS)
     - Macro [IOS_APP_ASSETS_FLAGS](#macro_IOS_APP_ASSETS_FLAGS)
     - Macro [IOS_APP_COMMON_FLAGS](#macro_IOS_APP_COMMON_FLAGS)
     - Macro [IOS_APP_SETTINGS](#macro_IOS_APP_SETTINGS)
     - Macro [IOS_ASSETS](#macro_IOS_ASSETS)
     - Macro [IWYU_MAPPING_FILE](#macro_IWYU_MAPPING_FILE)

     </details>

     <details><summary><b>J</b> &nbsp; <i>(21 macros)</i></summary>

     - Macro [JAR_ANNOTATION_PROCESSOR](#macro_JAR_ANNOTATION_PROCESSOR)
     - Macro [JAR_EXCLUDE](#macro_JAR_EXCLUDE)
     - Macro [JAR_MAIN_CLASS](#macro_JAR_MAIN_CLASS)
     - Macro [JAR_RESOURCE](#macro_JAR_RESOURCE)
     - Macro [JAVAC_FLAGS](#macro_JAVAC_FLAGS)
     - Macro [JAVA_DEPENDENCIES_CONFIGURATION](#macro_JAVA_DEPENDENCIES_CONFIGURATION)
     - Macro [JAVA_EXTERNAL_DEPENDENCIES](#macro_JAVA_EXTERNAL_DEPENDENCIES)
     - Macro [JAVA_IGNORE_CLASSPATH_CLASH_FOR](#macro_JAVA_IGNORE_CLASSPATH_CLASH_FOR)
     - Macro [JAVA_MODULE](#macro_JAVA_MODULE)
     - Macro [JAVA_PROTO_PLUGIN](#macro_JAVA_PROTO_PLUGIN)
     - Macro [JAVA_RESOURCE](#macro_JAVA_RESOURCE)
     - Macro [JAVA_RESOURCE_TAR](#macro_JAVA_RESOURCE_TAR)
     - Macro [JAVA_SRCS](#macro_JAVA_SRCS)
     - Macro [JAVA_TEST](#macro_JAVA_TEST)
     - Macro [JAVA_TEST_DEPS](#macro_JAVA_TEST_DEPS)
     - Macro [JDK_VERSION](#macro_JDK_VERSION)
     - Macro [JNI_EXPORTS](#macro_JNI_EXPORTS)
     - Macro [JOIN_SRCS](#macro_JOIN_SRCS)
     - Macro [JOIN_SRCS_GLOBAL](#macro_JOIN_SRCS_GLOBAL)
     - Macro [JUNIT_TESTS_JAR](#macro_JUNIT_TESTS_JAR)
     - Macro [JVM_ARGS](#macro_JVM_ARGS)

     </details>

     <details><summary><b>K</b> &nbsp; <i>(7 macros)</i></summary>

     - Macro [KAPT_ANNOTATION_PROCESSOR](#macro_KAPT_ANNOTATION_PROCESSOR)
     - Macro [KAPT_ANNOTATION_PROCESSOR_CLASSPATH](#macro_KAPT_ANNOTATION_PROCESSOR_CLASSPATH)
     - Macro [KAPT_ANNOTATION_PROCESSOR_OPTIONS](#macro_KAPT_ANNOTATION_PROCESSOR_OPTIONS)
     - Macro [KAPT_OPTS](#macro_KAPT_OPTS)
     - Macro [KOTLINC_FLAGS](#macro_KOTLINC_FLAGS)
     - Macro [KTLINT_BASELINE_FILE](#macro_KTLINT_BASELINE_FILE)
     - Macro [KTLINT_RULESET](#macro_KTLINT_RULESET)

     </details>

     <details><summary><b>L</b> &nbsp; <i>(22 macros)</i></summary>

     - Macro [LARGE_FILES](#macro_LARGE_FILES)
     - Macro [LDFLAGS](#macro_LDFLAGS)
     - Macro [LD_PLUGIN](#macro_LD_PLUGIN)
     - Macro [LICENSE](#macro_LICENSE)
     - Macro [LICENSE_RESTRICTION](#macro_LICENSE_RESTRICTION)
     - Macro [LICENSE_RESTRICTION_EXCEPTIONS](#macro_LICENSE_RESTRICTION_EXCEPTIONS)
     - Macro [LICENSE_TEXTS](#macro_LICENSE_TEXTS)
     - Macro [LINKER_SCRIPT](#macro_LINKER_SCRIPT)
     - Macro [LINK_EXCLUDE_LIBRARIES](#macro_LINK_EXCLUDE_LIBRARIES)
     - Macro [LINT](#macro_LINT)
     - Macro [LIST_PROTO](#macro_LIST_PROTO)
     - Macro [LJ_21_ARCHIVE](#macro_LJ_21_ARCHIVE)
     - Macro [LJ_ARCHIVE](#macro_LJ_ARCHIVE)
     - Macro [LLVM_BC](#macro_LLVM_BC)
     - Macro [LLVM_COMPILE_C](#macro_LLVM_COMPILE_C)
     - Macro [LLVM_COMPILE_CXX](#macro_LLVM_COMPILE_CXX)
     - Macro [LLVM_COMPILE_LL](#macro_LLVM_COMPILE_LL)
     - Macro [LLVM_LINK](#macro_LLVM_LINK)
     - Macro [LLVM_LLC](#macro_LLVM_LLC)
     - Macro [LLVM_OPT](#macro_LLVM_OPT)
     - Macro [LOCAL_JAR](#macro_LOCAL_JAR)
     - Macro [LOCAL_SOURCES_JAR](#macro_LOCAL_SOURCES_JAR)

     </details>

     <details><summary><b>M</b> &nbsp; <i>(6 macros)</i></summary>

     - Macro [MACROS_WITH_ERROR](#macro_MACROS_WITH_ERROR)
     - Macro [MANUAL_GENERATION](#macro_MANUAL_GENERATION)
     - Macro [MASMFLAGS](#macro_MASMFLAGS)
     - Macro [MAVEN_GROUP_ID](#macro_MAVEN_GROUP_ID)
     - Macro [MESSAGE](#macro_MESSAGE)
     - Macro [MODULEWISE_LICENSE_RESTRICTION](#macro_MODULEWISE_LICENSE_RESTRICTION)

     </details>

     <details><summary><b>N</b> &nbsp; <i>(40 macros)</i></summary>

     - Macro [NEED_CHECK](#macro_NEED_CHECK)
     - Macro [NEED_REVIEW](#macro_NEED_REVIEW)
     - Macro [NGINX_MODULES](#macro_NGINX_MODULES)
     - Macro [NO_BUILD_IF](#macro_NO_BUILD_IF)
     - Macro [NO_CHECK_IMPORTS](#macro_NO_CHECK_IMPORTS)
     - Macro [NO_CLANG_COVERAGE](#macro_NO_CLANG_COVERAGE)
     - Macro [NO_CLANG_MCDC_COVERAGE](#macro_NO_CLANG_MCDC_COVERAGE)
     - Macro [NO_CLANG_TIDY](#macro_NO_CLANG_TIDY)
     - Macro [NO_COMPILER_WARNINGS](#macro_NO_COMPILER_WARNINGS)
     - Macro [NO_COW](#macro_NO_COW)
     - Macro [NO_CPU_CHECK](#macro_NO_CPU_CHECK)
     - Macro [NO_CUDA_NVPRUNE](#macro_NO_CUDA_NVPRUNE)
     - Macro [NO_CYTHON_COVERAGE](#macro_NO_CYTHON_COVERAGE)
     - Macro [NO_DEBUG_INFO](#macro_NO_DEBUG_INFO)
     - Macro [NO_DOCTESTS](#macro_NO_DOCTESTS)
     - Macro [NO_EXPORT_DYNAMIC_SYMBOLS](#macro_NO_EXPORT_DYNAMIC_SYMBOLS)
     - Macro [NO_EXTENDED_SOURCE_SEARCH](#macro_NO_EXTENDED_SOURCE_SEARCH)
     - Macro [NO_IMPORT_TRACING](#macro_NO_IMPORT_TRACING)
     - Macro [NO_IWYU](#macro_NO_IWYU)
     - Macro [NO_JOIN_SRC](#macro_NO_JOIN_SRC)
     - Macro [NO_LIBC](#macro_NO_LIBC)
     - Macro [NO_LINT](#macro_NO_LINT)
     - Macro [NO_LTO](#macro_NO_LTO)
     - Macro [NO_MYPY](#macro_NO_MYPY)
     - Macro [NO_NEED_CHECK](#macro_NO_NEED_CHECK)
     - Macro [NO_OPTIMIZE](#macro_NO_OPTIMIZE)
     - Macro [NO_OPTIMIZE_PY_PROTOS](#macro_NO_OPTIMIZE_PY_PROTOS)
     - Macro [NO_PLATFORM](#macro_NO_PLATFORM)
     - Macro [NO_PROFILE_RUNTIME](#macro_NO_PROFILE_RUNTIME)
     - Macro [NO_PYTHON_COVERAGE](#macro_NO_PYTHON_COVERAGE)
     - Macro [NO_RUNTIME](#macro_NO_RUNTIME)
     - Macro [NO_SANITIZE](#macro_NO_SANITIZE)
     - Macro [NO_SANITIZE_COVERAGE](#macro_NO_SANITIZE_COVERAGE)
     - Macro [NO_SPLIT_DWARF](#macro_NO_SPLIT_DWARF)
     - Macro [NO_SSE4](#macro_NO_SSE4)
     - Macro [NO_TS_TYPECHECK](#macro_NO_TS_TYPECHECK)
     - Macro [NO_UTIL](#macro_NO_UTIL)
     - Macro [NO_WSHADOW](#macro_NO_WSHADOW)
     - Macro [NO_YMAKE_PYTHON3](#macro_NO_YMAKE_PYTHON3)
     - Macro [NVCC_DEVICE_LINK](#macro_NVCC_DEVICE_LINK)

     </details>

     <details><summary><b>O</b> &nbsp; <i>(5 macros)</i></summary>

     - Macro [OBJC_FLAGS](#macro_OBJC_FLAGS)
     - Macro [ONLY_TAGS](#macro_ONLY_TAGS)
     - Macro [OPENSOURCE_EXPORT_REPLACEMENT](#macro_OPENSOURCE_EXPORT_REPLACEMENT)
     - Macro [OPENSOURCE_EXPORT_REPLACEMENT_BY_OS](#macro_OPENSOURCE_EXPORT_REPLACEMENT_BY_OS)
     - Macro [ORIGINAL_SOURCE](#macro_ORIGINAL_SOURCE)

     </details>

     <details><summary><b>P</b> &nbsp; <i>(38 macros)</i></summary>

     - Macro [PACK](#macro_PACK)
     - Macro [PARALLEL_TESTS_WITHIN_NODE](#macro_PARALLEL_TESTS_WITHIN_NODE)
     - Macro [PARTITIONED_RECURSE](#macro_PARTITIONED_RECURSE)
     - Macro [PARTITIONED_RECURSE_FOR_TESTS](#macro_PARTITIONED_RECURSE_FOR_TESTS)
     - Macro [PARTITIONED_RECURSE_ROOT_RELATIVE](#macro_PARTITIONED_RECURSE_ROOT_RELATIVE)
     - Macro [PEERDIR](#macro_PEERDIR)
     - Macro [PIRE_INLINE](#macro_PIRE_INLINE)
     - Macro [PIRE_INLINE_CMD](#macro_PIRE_INLINE_CMD)
     - Macro [POPULATE_CPP_COVERAGE_FLAGS](#macro_POPULATE_CPP_COVERAGE_FLAGS)
     - Macro [POPULATE_CPP_YNDEXING](#macro_POPULATE_CPP_YNDEXING)
     - Macro [PREPARE_INDUCED_DEPS](#macro_PREPARE_INDUCED_DEPS)
     - Macro [PROCESSOR_CLASSES](#macro_PROCESSOR_CLASSES)
     - Macro [PROCESS_DOCS](#macro_PROCESS_DOCS)
     - Macro [PROCESS_MKDOCS](#macro_PROCESS_MKDOCS)
     - Macro [PROTO2FBS](#macro_PROTO2FBS)
     - Macro [PROTOC_FATAL_WARNINGS](#macro_PROTOC_FATAL_WARNINGS)
     - Macro [PROTO_ADDINCL](#macro_PROTO_ADDINCL)
     - Macro [PROTO_CMD](#macro_PROTO_CMD)
     - Macro [PROTO_NAMESPACE](#macro_PROTO_NAMESPACE)
     - Macro [PROTO_TO_NAMESPACE](#macro_PROTO_TO_NAMESPACE)
     - Macro [PROVIDES](#macro_PROVIDES)
     - Macro [PYTHON2_ADDINCL](#macro_PYTHON2_ADDINCL)
     - Macro [PYTHON2_MODULE](#macro_PYTHON2_MODULE)
     - Macro [PYTHON3_ADDINCL](#macro_PYTHON3_ADDINCL)
     - Macro [PYTHON3_MODULE](#macro_PYTHON3_MODULE)
     - Macro [PYTHON_PATH](#macro_PYTHON_PATH)
     - Macro [PY_CONSTRUCTOR](#macro_PY_CONSTRUCTOR)
     - Macro [PY_DOCTESTS](#macro_PY_DOCTESTS)
     - Macro [PY_ENUMS_SERIALIZATION](#macro_PY_ENUMS_SERIALIZATION)
     - Macro [PY_EXTRALIBS](#macro_PY_EXTRALIBS)
     - Macro [PY_EXTRA_LINT_FILES](#macro_PY_EXTRA_LINT_FILES)
     - Macro [PY_MAIN](#macro_PY_MAIN)
     - Macro [PY_NAMESPACE](#macro_PY_NAMESPACE)
     - Macro [PY_PROTOS_FOR](#macro_PY_PROTOS_FOR)
     - Macro [PY_PROTO_PLUGIN](#macro_PY_PROTO_PLUGIN)
     - Macro [PY_PROTO_PLUGIN2](#macro_PY_PROTO_PLUGIN2)
     - Macro [PY_REGISTER](#macro_PY_REGISTER)
     - Macro [PY_SRCS](#macro_PY_SRCS)

     </details>

     <details><summary><b>R</b> &nbsp; <i>(29 macros)</i></summary>

     - Macro [RECURSE](#macro_RECURSE)
     - Macro [RECURSE_FOR_TESTS](#macro_RECURSE_FOR_TESTS)
     - Macro [RECURSE_ROOT_RELATIVE](#macro_RECURSE_ROOT_RELATIVE)
     - Macro [REGISTER_SANDBOX_IMPORT](#macro_REGISTER_SANDBOX_IMPORT)
     - Macro [REGISTER_YQL_PYTHON_UDF](#macro_REGISTER_YQL_PYTHON_UDF)
     - Macro [REQUIREMENTS](#macro_REQUIREMENTS)
     - Macro [REQUIRES](#macro_REQUIRES)
     - Macro [REQUIRE_RESOURCE](#macro_REQUIRE_RESOURCE)
     - Macro [RESOLVE_PROTO](#macro_RESOLVE_PROTO)
     - Macro [RESOURCE](#macro_RESOURCE)
     - Macro [RESOURCE_FILES](#macro_RESOURCE_FILES)
     - Macro [RESTRICT_PATH](#macro_RESTRICT_PATH)
     - Macro [RISK_GEN_DATA_MODEL](#macro_RISK_GEN_DATA_MODEL)
     - Macro [ROS_SRCS](#macro_ROS_SRCS)
     - Macro [RUN](#macro_RUN)
     - Macro [RUN_ANTLR](#macro_RUN_ANTLR)
     - Macro [RUN_ANTLR4](#macro_RUN_ANTLR4)
     - Macro [RUN_ANTLR4_CPP](#macro_RUN_ANTLR4_CPP)
     - Macro [RUN_ANTLR4_CPP_SPLIT](#macro_RUN_ANTLR4_CPP_SPLIT)
     - Macro [RUN_ANTLR4_GO](#macro_RUN_ANTLR4_GO)
     - Macro [RUN_ANTLR4_PYTHON2](#macro_RUN_ANTLR4_PYTHON2)
     - Macro [RUN_ANTLR4_PYTHON3](#macro_RUN_ANTLR4_PYTHON3)
     - Macro [RUN_JAVASCRIPT](#macro_RUN_JAVASCRIPT)
     - Macro [RUN_JAVASCRIPT_AFTER_BUILD](#macro_RUN_JAVASCRIPT_AFTER_BUILD)
     - Macro [RUN_JAVA_PROGRAM](#macro_RUN_JAVA_PROGRAM)
     - Macro [RUN_LUA](#macro_RUN_LUA)
     - Macro [RUN_PROGRAM](#macro_RUN_PROGRAM)
     - Macro [RUN_PY3_PROGRAM](#macro_RUN_PY3_PROGRAM)
     - Macro [RUN_PYTHON3](#macro_RUN_PYTHON3)

     </details>

     <details><summary><b>S</b> &nbsp; <i>(55 macros)</i></summary>

     - Macro [SDBUS_CPP_ADAPTOR](#macro_SDBUS_CPP_ADAPTOR)
     - Macro [SDBUS_CPP_PROXY](#macro_SDBUS_CPP_PROXY)
     - Macro [SDC_DIAGS_SPLIT_GENERATOR_V3](#macro_SDC_DIAGS_SPLIT_GENERATOR_V3)
     - Macro [SDC_DIAGS_SPLIT_GENERATOR_V4](#macro_SDC_DIAGS_SPLIT_GENERATOR_V4)
     - Macro [SDC_INSTALL](#macro_SDC_INSTALL)
     - Macro [SELECT_CLANG_SA_CONFIG](#macro_SELECT_CLANG_SA_CONFIG)
     - Macro [SELECT_PROTO_LAYOUT](#macro_SELECT_PROTO_LAYOUT)
     - Macro [SET](#macro_SET)
     - Macro [SETUP_EXECTEST](#macro_SETUP_EXECTEST)
     - Macro [SETUP_PYTEST_BIN](#macro_SETUP_PYTEST_BIN)
     - Macro [SETUP_RUN_PYTHON](#macro_SETUP_RUN_PYTHON)
     - Macro [SET_APPEND](#macro_SET_APPEND)
     - Macro [SET_APPEND_WITH_GLOBAL](#macro_SET_APPEND_WITH_GLOBAL)
     - Macro [SET_COMPILE_OUTPUTS_MODIFIERS](#macro_SET_COMPILE_OUTPUTS_MODIFIERS)
     - Macro [SET_CPP_COVERAGE_FLAGS](#macro_SET_CPP_COVERAGE_FLAGS)
     - Macro [SET_CUSTOM_CLANG_TIDY](#macro_SET_CUSTOM_CLANG_TIDY)
     - Macro [SET_RESOURCE_MAP_FROM_JSON](#macro_SET_RESOURCE_MAP_FROM_JSON)
     - Macro [SET_RESOURCE_URI_FROM_JSON](#macro_SET_RESOURCE_URI_FROM_JSON)
     - Macro [SIZE](#macro_SIZE)
     - Macro [SKIP_TEST](#macro_SKIP_TEST)
     - Macro [SOURCE_GROUP](#macro_SOURCE_GROUP)
     - Macro [SPLIT_CODEGEN](#macro_SPLIT_CODEGEN)
     - Macro [SPLIT_DWARF](#macro_SPLIT_DWARF)
     - Macro [SPLIT_FACTOR](#macro_SPLIT_FACTOR)
     - Macro [SRC](#macro_SRC)
     - Macro [SRCDIR](#macro_SRCDIR)
     - Macro [SRCS](#macro_SRCS)
     - Macro [SRC_C_AMX](#macro_SRC_C_AMX)
     - Macro [SRC_C_AVX](#macro_SRC_C_AVX)
     - Macro [SRC_C_AVX2](#macro_SRC_C_AVX2)
     - Macro [SRC_C_AVX512](#macro_SRC_C_AVX512)
     - Macro [SRC_C_NO_LTO](#macro_SRC_C_NO_LTO)
     - Macro [SRC_C_PIC](#macro_SRC_C_PIC)
     - Macro [SRC_C_SSE2](#macro_SRC_C_SSE2)
     - Macro [SRC_C_SSE3](#macro_SRC_C_SSE3)
     - Macro [SRC_C_SSE4](#macro_SRC_C_SSE4)
     - Macro [SRC_C_SSE41](#macro_SRC_C_SSE41)
     - Macro [SRC_C_SSSE3](#macro_SRC_C_SSSE3)
     - Macro [SRC_C_XOP](#macro_SRC_C_XOP)
     - Macro [SRC_RESOURCE](#macro_SRC_RESOURCE)
     - Macro [STRIP](#macro_STRIP)
     - Macro [STYLE_CPP](#macro_STYLE_CPP)
     - Macro [STYLE_DETEKT](#macro_STYLE_DETEKT)
     - Macro [STYLE_DUMMY](#macro_STYLE_DUMMY)
     - Macro [STYLE_FLAKE8](#macro_STYLE_FLAKE8)
     - Macro [STYLE_JSON](#macro_STYLE_JSON)
     - Macro [STYLE_PY2_FLAKE8](#macro_STYLE_PY2_FLAKE8)
     - Macro [STYLE_PYTHON](#macro_STYLE_PYTHON)
     - Macro [STYLE_RUFF](#macro_STYLE_RUFF)
     - Macro [STYLE_YAML](#macro_STYLE_YAML)
     - Macro [STYLE_YQL](#macro_STYLE_YQL)
     - Macro [SUBSCRIBER](#macro_SUBSCRIBER)
     - Macro [SUPPRESSIONS](#macro_SUPPRESSIONS)
     - Macro [SYMLINK](#macro_SYMLINK)
     - Macro [SYSTEM_PROPERTIES](#macro_SYSTEM_PROPERTIES)

     </details>

     <details><summary><b>T</b> &nbsp; <i>(43 macros)</i></summary>

     - Macro [TAG](#macro_TAG)
     - Macro [TASKLET](#macro_TASKLET)
     - Macro [TASKLET_REG](#macro_TASKLET_REG)
     - Macro [TASKLET_REG_EXT](#macro_TASKLET_REG_EXT)
     - Macro [TEST_CWD](#macro_TEST_CWD)
     - Macro [TEST_DATA](#macro_TEST_DATA)
     - Macro [TEST_JAVA_CLASSPATH_CMD_TYPE](#macro_TEST_JAVA_CLASSPATH_CMD_TYPE)
     - Macro [TEST_SRCS](#macro_TEST_SRCS)
     - Macro [THINLTO_CACHE](#macro_THINLTO_CACHE)
     - Macro [TIMEOUT](#macro_TIMEOUT)
     - Macro [TOOLCHAIN](#macro_TOOLCHAIN)
     - Macro [TS_BIOME](#macro_TS_BIOME)
     - Macro [TS_BUILD_ENV](#macro_TS_BUILD_ENV)
     - Macro [TS_BUILD_OUTPUTS](#macro_TS_BUILD_OUTPUTS)
     - Macro [TS_BUILD_SCRIPT](#macro_TS_BUILD_SCRIPT)
     - Macro [TS_CONFIG](#macro_TS_CONFIG)
     - Macro [TS_ESLINT_CONFIG](#macro_TS_ESLINT_CONFIG)
     - Macro [TS_EXCLUDE_FILES_GLOB](#macro_TS_EXCLUDE_FILES_GLOB)
     - Macro [TS_FILES](#macro_TS_FILES)
     - Macro [TS_FILES_GLOB](#macro_TS_FILES_GLOB)
     - Macro [TS_LARGE_FILES](#macro_TS_LARGE_FILES)
     - Macro [TS_LINT](#macro_TS_LINT)
     - Macro [TS_NEXT_BUILD_OPTIONS](#macro_TS_NEXT_BUILD_OPTIONS)
     - Macro [TS_NEXT_CONFIG](#macro_TS_NEXT_CONFIG)
     - Macro [TS_NEXT_EXPERIMENTAL_BUILD_MODE](#macro_TS_NEXT_EXPERIMENTAL_BUILD_MODE)
     - Macro [TS_NEXT_OUTPUT](#macro_TS_NEXT_OUTPUT)
     - Macro [TS_PROTO_OPT](#macro_TS_PROTO_OPT)
     - Macro [TS_PROTO_PACKAGE_NAME](#macro_TS_PROTO_PACKAGE_NAME)
     - Macro [TS_RSPACK_CONFIG](#macro_TS_RSPACK_CONFIG)
     - Macro [TS_RSPACK_OUTPUT](#macro_TS_RSPACK_OUTPUT)
     - Macro [TS_STYLELINT](#macro_TS_STYLELINT)
     - Macro [TS_TEST](#macro_TS_TEST)
     - Macro [TS_TEST_CONFIG](#macro_TS_TEST_CONFIG)
     - Macro [TS_TEST_DATA](#macro_TS_TEST_DATA)
     - Macro [TS_TEST_DEPENDS_ON_BUILD](#macro_TS_TEST_DEPENDS_ON_BUILD)
     - Macro [TS_TEST_INCLUDE_NODEJS](#macro_TS_TEST_INCLUDE_NODEJS)
     - Macro [TS_TEST_SRCS](#macro_TS_TEST_SRCS)
     - Macro [TS_TYPECHECK](#macro_TS_TYPECHECK)
     - Macro [TS_USE_BUN](#macro_TS_USE_BUN)
     - Macro [TS_VITE_CONFIG](#macro_TS_VITE_CONFIG)
     - Macro [TS_VITE_OUTPUT](#macro_TS_VITE_OUTPUT)
     - Macro [TS_WEBPACK_CONFIG](#macro_TS_WEBPACK_CONFIG)
     - Macro [TS_WEBPACK_OUTPUT](#macro_TS_WEBPACK_OUTPUT)

     </details>

     <details><summary><b>U</b> &nbsp; <i>(41 macros)</i></summary>

     - Macro [UBERJAR](#macro_UBERJAR)
     - Macro [UBERJAR_APPENDING_TRANSFORMER](#macro_UBERJAR_APPENDING_TRANSFORMER)
     - Macro [UBERJAR_HIDE_EXCLUDE_PATTERN](#macro_UBERJAR_HIDE_EXCLUDE_PATTERN)
     - Macro [UBERJAR_HIDE_INCLUDE_PATTERN](#macro_UBERJAR_HIDE_INCLUDE_PATTERN)
     - Macro [UBERJAR_HIDING_PREFIX](#macro_UBERJAR_HIDING_PREFIX)
     - Macro [UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE](#macro_UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE)
     - Macro [UBERJAR_MANIFEST_TRANSFORMER_MAIN](#macro_UBERJAR_MANIFEST_TRANSFORMER_MAIN)
     - Macro [UBERJAR_PATH_EXCLUDE_PREFIX](#macro_UBERJAR_PATH_EXCLUDE_PREFIX)
     - Macro [UBERJAR_SERVICES_RESOURCE_TRANSFORMER](#macro_UBERJAR_SERVICES_RESOURCE_TRANSFORMER)
     - Macro [UDF_NO_PROBE](#macro_UDF_NO_PROBE)
     - Macro [UDF_NO_SCAN](#macro_UDF_NO_SCAN)
     - Macro [UPDATE_VCS_JAVA_INFO_NODEP](#macro_UPDATE_VCS_JAVA_INFO_NODEP)
     - Macro [USE_ANNOTATION_PROCESSOR](#macro_USE_ANNOTATION_PROCESSOR)
     - Macro [USE_COMMON_GOOGLE_APIS](#macro_USE_COMMON_GOOGLE_APIS)
     - Macro [USE_CXX](#macro_USE_CXX)
     - Macro [USE_DYNAMIC_CUDA](#macro_USE_DYNAMIC_CUDA)
     - Macro [USE_ERROR_PRONE](#macro_USE_ERROR_PRONE)
     - Macro [USE_JAVALITE](#macro_USE_JAVALITE)
     - Macro [USE_KTLINT_OLD](#macro_USE_KTLINT_OLD)
     - Macro [USE_LEGACY_PNPM_VIRTUAL_STORE](#macro_USE_LEGACY_PNPM_VIRTUAL_STORE)
     - Macro [USE_LINKER_GOLD](#macro_USE_LINKER_GOLD)
     - Macro [USE_LLVM_BC16](#macro_USE_LLVM_BC16)
     - Macro [USE_LLVM_BC18](#macro_USE_LLVM_BC18)
     - Macro [USE_LLVM_BC20](#macro_USE_LLVM_BC20)
     - Macro [USE_MODERN_FLEX](#macro_USE_MODERN_FLEX)
     - Macro [USE_MODERN_FLEX_WITH_HEADER](#macro_USE_MODERN_FLEX_WITH_HEADER)
     - Macro [USE_NASM](#macro_USE_NASM)
     - Macro [USE_OLD_FLEX](#macro_USE_OLD_FLEX)
     - Macro [USE_PERSISTENT_RECIPE](#macro_USE_PERSISTENT_RECIPE)
     - Macro [USE_PLANTUML](#macro_USE_PLANTUML)
     - Macro [USE_PYTHON2](#macro_USE_PYTHON2)
     - Macro [USE_PYTHON3](#macro_USE_PYTHON3)
     - Macro [USE_RECIPE](#macro_USE_RECIPE)
     - Macro [USE_SA_PLUGINS](#macro_USE_SA_PLUGINS)
     - Macro [USE_SKIFF](#macro_USE_SKIFF)
     - Macro [USE_UTIL](#macro_USE_UTIL)
     - Macro [USRV_GEN_GRPC_CLIENT_V2](#macro_USRV_GEN_GRPC_CLIENT_V2)
     - Macro [USRV_GEN_GRPC_CLIENT_V2_STRUCTS](#macro_USRV_GEN_GRPC_CLIENT_V2_STRUCTS)
     - Macro [USRV_GEN_GRPC_SERVICE_V2](#macro_USRV_GEN_GRPC_SERVICE_V2)
     - Macro [USRV_GEN_GRPC_SERVICE_V2_STRUCTS](#macro_USRV_GEN_GRPC_SERVICE_V2_STRUCTS)
     - Macro [USRV_GEN_PROTO_STRUCTS](#macro_USRV_GEN_PROTO_STRUCTS)

     </details>

     <details><summary><b>V</b> &nbsp; <i>(6 macros)</i></summary>

     - Macro [VALIDATE_DATA_RESTART](#macro_VALIDATE_DATA_RESTART)
     - Macro [VALIDATE_IN_DIRS](#macro_VALIDATE_IN_DIRS)
     - Macro [VCS_INFO_FILE](#macro_VCS_INFO_FILE)
     - Macro [VERSION](#macro_VERSION)
     - Macro [VISIBILITY](#macro_VISIBILITY)
     - Macro [VITE_OUTPUT](#macro_VITE_OUTPUT)

     </details>

     <details><summary><b>W</b> &nbsp; <i>(17 macros)</i></summary>

     - Macro [WEBPACK_OUTPUT](#macro_WEBPACK_OUTPUT)
     - Macro [WINDOWS_LONG_PATH_MANIFEST](#macro_WINDOWS_LONG_PATH_MANIFEST)
     - Macro [WINDOWS_MANIFEST](#macro_WINDOWS_MANIFEST)
     - Macro [WITHOUT_LICENSE_TEXTS](#macro_WITHOUT_LICENSE_TEXTS)
     - Macro [WITHOUT_VERSION](#macro_WITHOUT_VERSION)
     - Macro [WITH_DYNAMIC_LIBS](#macro_WITH_DYNAMIC_LIBS)
     - Macro [WITH_JDK](#macro_WITH_JDK)
     - Macro [WITH_KAPT](#macro_WITH_KAPT)
     - Macro [WITH_KOTLIN](#macro_WITH_KOTLIN)
     - Macro [WITH_KOTLINC_ALLOPEN](#macro_WITH_KOTLINC_ALLOPEN)
     - Macro [WITH_KOTLINC_DETEKT](#macro_WITH_KOTLINC_DETEKT)
     - Macro [WITH_KOTLINC_LOMBOK](#macro_WITH_KOTLINC_LOMBOK)
     - Macro [WITH_KOTLINC_NOARG](#macro_WITH_KOTLINC_NOARG)
     - Macro [WITH_KOTLINC_SERIALIZATION](#macro_WITH_KOTLINC_SERIALIZATION)
     - Macro [WITH_KOTLIN_GRPC](#macro_WITH_KOTLIN_GRPC)
     - Macro [WITH_NODE_MODULES](#macro_WITH_NODE_MODULES)
     - Macro [WITH_YA_1931](#macro_WITH_YA_1931)

     </details>

     <details><summary><b>Y</b> &nbsp; <i>(10 macros)</i></summary>

     - Macro [YABS_GENERATE_CONF](#macro_YABS_GENERATE_CONF)
     - Macro [YABS_GENERATE_PHANTOM_CONF_PATCH](#macro_YABS_GENERATE_PHANTOM_CONF_PATCH)
     - Macro [YABS_GENERATE_PHANTOM_CONF_TEST_CHECK](#macro_YABS_GENERATE_PHANTOM_CONF_TEST_CHECK)
     - Macro [YA_CONF_JSON](#macro_YA_CONF_JSON)
     - Macro [YDL_DESC_USE_BINARY](#macro_YDL_DESC_USE_BINARY)
     - Macro [YQL_ABI_VERSION](#macro_YQL_ABI_VERSION)
     - Macro [YQL_LAST_ABI_VERSION](#macro_YQL_LAST_ABI_VERSION)
     - Macro [YT_ORM_PROTO_YSON](#macro_YT_ORM_PROTO_YSON)
     - Macro [YT_RECORD_DISABLE_PEERDIR](#macro_YT_RECORD_DISABLE_PEERDIR)
     - Macro [YT_SPEC](#macro_YT_SPEC)

     </details>
   * [Properties](#properties)
       - Property [ALIASES](#property_ALIASES)
       - Property [ALLOWED](#property_ALLOWED)
       - Property [ALLOWED_IN_LINTERS_MAKE](#property_ALLOWED_IN_LINTERS_MAKE)
       - Property [ARGS_PARSER](#property_ARGS_PARSER)
       - Property [CMD](#property_CMD)
       - Property [DEFAULT_NAME_GENERATOR](#property_DEFAULT_NAME_GENERATOR)
       - Property [EPILOGUE](#property_EPILOGUE)
       - Property [EXTS](#property_EXTS)
       - Property [FILE_GROUP](#property_FILE_GROUP)
       - Property [FINAL_TARGET](#property_FINAL_TARGET)
       - Property [GEN_FROM_FILE](#property_GEN_FROM_FILE)
       - Property [GLOBAL](#property_GLOBAL)
       - Property [GLOBAL_CMD](#property_GLOBAL_CMD)
       - Property [GLOBAL_EXTS](#property_GLOBAL_EXTS)
       - Property [GLOBAL_SEM](#property_GLOBAL_SEM)
       - Property [IGNORED](#property_IGNORED)
       - Property [INCLUDE_TAG](#property_INCLUDE_TAG)
       - Property [NODE_TYPE](#property_NODE_TYPE)
       - Property [NO_EXPAND](#property_NO_EXPAND)
       - Property [PEERDIRSELF](#property_PEERDIRSELF)
       - Property [PEERDIR_POLICY](#property_PEERDIR_POLICY)
       - Property [PROXY](#property_PROXY)
       - Property [RESTRICTED](#property_RESTRICTED)
       - Property [SEM](#property_SEM)
       - Property [SYMLINK_POLICY](#property_SYMLINK_POLICY)
       - Property [TRANSITION](#property_TRANSITION)
       - Property [USE_PEERS_LATE_OUTS](#property_USE_PEERS_LATE_OUTS)
       - Property [VERSION_PROXY](#property_VERSION_PROXY)
   * [Variables](#variables)
       - Variable [APPLIED_EXCLUDES](#variable_APPLIED_EXCLUDES)
       - Variable [ARCADIA_BUILD_ROOT](#variable_ARCADIA_BUILD_ROOT)
       - Variable [ARCADIA_ROOT](#variable_ARCADIA_ROOT)
       - Variable [AUTO_INPUT](#variable_AUTO_INPUT)
       - Variable [BINDIR](#variable_BINDIR)
       - Variable [CHECK_INTERNAL](#variable_CHECK_INTERNAL)
       - Variable [CMAKE_CURRENT_BINARY_DIR](#variable_CMAKE_CURRENT_BINARY_DIR)
       - Variable [CMAKE_CURRENT_SOURCE_DIR](#variable_CMAKE_CURRENT_SOURCE_DIR)
       - Variable [CONSUME_NON_MANAGEABLE_PEERS](#variable_CONSUME_NON_MANAGEABLE_PEERS)
       - Variable [CURDIR](#variable_CURDIR)
       - Variable [DART_CLASSPATH](#variable_DART_CLASSPATH)
       - Variable [DART_CLASSPATH_DEPS](#variable_DART_CLASSPATH_DEPS)
       - Variable [DEFAULT_MODULE_LICENSE](#variable_DEFAULT_MODULE_LICENSE)
       - Variable [DEPENDENCY_MANAGEMENT_TAGS_EXCLUDE](#variable_DEPENDENCY_MANAGEMENT_TAGS_EXCLUDE)
       - Variable [DEPENDENCY_MANAGEMENT_TRANSPARENT](#variable_DEPENDENCY_MANAGEMENT_TRANSPARENT)
       - Variable [DEPENDENCY_MANAGEMENT_VALUE](#variable_DEPENDENCY_MANAGEMENT_VALUE)
       - Variable [DONT_RESOLVE_INCLUDES](#variable_DONT_RESOLVE_INCLUDES)
       - Variable [DYNAMIC_LINK](#variable_DYNAMIC_LINK)
       - Variable [EV_HEADER_EXTS](#variable_EV_HEADER_EXTS)
       - Variable [EXCLUDE_SUBMODULES](#variable_EXCLUDE_SUBMODULES)
       - Variable [EXCLUDE_VALUE](#variable_EXCLUDE_VALUE)
       - Variable [EXPORTED_BUILD_SYSTEM_BUILD_ROOT](#variable_EXPORTED_BUILD_SYSTEM_BUILD_ROOT)
       - Variable [EXPORTED_BUILD_SYSTEM_SOURCE_ROOT](#variable_EXPORTED_BUILD_SYSTEM_SOURCE_ROOT)
       - Variable [GLOBAL_SUFFIX](#variable_GLOBAL_SUFFIX)
       - Variable [GLOBAL_TARGET](#variable_GLOBAL_TARGET)
       - Variable [GO_HAS_INTERNAL_TESTS](#variable_GO_HAS_INTERNAL_TESTS)
       - Variable [GO_TEST_FOR_DIR](#variable_GO_TEST_FOR_DIR)
       - Variable [HAS_MANAGEABLE_PEERS](#variable_HAS_MANAGEABLE_PEERS)
       - Variable [IGNORE_JAVA_DEPENDENCIES_CONFIGURATION](#variable_IGNORE_JAVA_DEPENDENCIES_CONFIGURATION)
       - Variable [INPUT](#variable_INPUT)
       - Variable [INTERNAL_EXCEPTIONS](#variable_INTERNAL_EXCEPTIONS)
       - Variable [JAVA_DEPENDENCIES_CONFIGURATION_VALUE](#variable_JAVA_DEPENDENCIES_CONFIGURATION_VALUE)
       - Variable [MANAGED_PEERS](#variable_MANAGED_PEERS)
       - Variable [MANAGED_PEERS_CLOSURE](#variable_MANAGED_PEERS_CLOSURE)
       - Variable [MANGLED_MODULE_TYPE](#variable_MANGLED_MODULE_TYPE)
       - Variable [MODDIR](#variable_MODDIR)
       - Variable [MODULE_ARGS](#variable_MODULE_ARGS)
       - Variable [MODULE_COMMON_CONFIGS_DIR](#variable_MODULE_COMMON_CONFIGS_DIR)
       - Variable [MODULE_KIND](#variable_MODULE_KIND)
       - Variable [MODULE_LANG](#variable_MODULE_LANG)
       - Variable [MODULE_PREFIX](#variable_MODULE_PREFIX)
       - Variable [MODULE_SEM_IGNORE](#variable_MODULE_SEM_IGNORE)
       - Variable [MODULE_SUFFIX](#variable_MODULE_SUFFIX)
       - Variable [MODULE_TYPE](#variable_MODULE_TYPE)
       - Variable [NON_NAMAGEABLE_PEERS](#variable_NON_NAMAGEABLE_PEERS)
       - Variable [OUTPUT](#variable_OUTPUT)
       - Variable [PASS_PEERS](#variable_PASS_PEERS)
       - Variable [PEERDIR_TAGS](#variable_PEERDIR_TAGS)
       - Variable [PEERS](#variable_PEERS)
       - Variable [PEERS_LATE_OUTS](#variable_PEERS_LATE_OUTS)
       - Variable [PROTO_HEADER_EXTS](#variable_PROTO_HEADER_EXTS)
       - Variable [PYTHON_BIN](#variable_PYTHON_BIN)
       - Variable [REALPRJNAME](#variable_REALPRJNAME)
       - Variable [SONAME](#variable_SONAME)
       - Variable [SRCS_GLOBAL](#variable_SRCS_GLOBAL)
       - Variable [START_TARGET](#variable_START_TARGET)
       - Variable [TARGET](#variable_TARGET)
       - Variable [TEST_CASE_ROOT](#variable_TEST_CASE_ROOT)
       - Variable [TEST_OUT_ROOT](#variable_TEST_OUT_ROOT)
       - Variable [TEST_SOURCE_ROOT](#variable_TEST_SOURCE_ROOT)
       - Variable [TEST_WORK_ROOT](#variable_TEST_WORK_ROOT)
       - Variable [TOOLS](#variable_TOOLS)
       - Variable [TS_CONFIG_DECLARATION](#variable_TS_CONFIG_DECLARATION)
       - Variable [TS_CONFIG_DECLARATION_MAP](#variable_TS_CONFIG_DECLARATION_MAP)
       - Variable [TS_CONFIG_DEDUCE_OUT](#variable_TS_CONFIG_DEDUCE_OUT)
       - Variable [TS_CONFIG_OUT_DIR](#variable_TS_CONFIG_OUT_DIR)
       - Variable [TS_CONFIG_PRESERVE_JSX](#variable_TS_CONFIG_PRESERVE_JSX)
       - Variable [TS_CONFIG_ROOT_DIR](#variable_TS_CONFIG_ROOT_DIR)
       - Variable [TS_CONFIG_SOURCE_MAP](#variable_TS_CONFIG_SOURCE_MAP)
       - Variable [UNITTEST_DIR](#variable_UNITTEST_DIR)
       - Variable [UNITTEST_MOD](#variable_UNITTEST_MOD)
       - Variable [USE_ALL_SRCS](#variable_USE_ALL_SRCS)
       - Variable [USE_GLOBAL_CMD](#variable_USE_GLOBAL_CMD)

## Multimodules <a name="multimodules"></a>

### Multimodule [DLL_JAVA](https://a.yandex-team.ru/arcadia/build/conf/swig.conf?rev=20020720#L90) <a name="multimodule_DLL_JAVA"></a>

```ya.make
DLL_JAVA()
```

DLL built using swig for Java. Produces dynamic library and a .jar.
Dynamic library is treated the same as in the case of PEERDIR from Java to DLL.
.jar goes on the classpath.

**Documentation:** https://wiki.yandex-team.ru/yatool/java/#integracijascpp/pythonsborkojj

### Multimodule [DOCS](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L155) <a name="multimodule_DOCS"></a>

```ya.make
DOCS()
```

Documentation project multimodule.

When built directly, via RECURSE, DEPENDS or BUNDLE the output artifact is docs.tar.gz with statically generated site.
When PEERDIRed from other DOCS() module behaves like a UNION (supplying own content and dependencies to build target).
Peerdirs from modules other than DOCS are not accepted.
Most usual macros are not accepted, only used with the macros DOCS_DIR(), DOCS_CONFIG(), DOCS_VARS().

**See also:** [DOCS_DIR()](#macro_DOCS_DIR), [DOCS_CONFIG()](#macro_DOCS_CONFIG), [DOCS_VARS()](#macro_DOCS_VARS)

### Multimodule [FBS_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L113) <a name="multimodule_FBS_LIBRARY"></a>

```ya.make
FBS_LIBRARY()
```

Build some variant of Flatbuffers library.

The particular variant is selected based on where PEERDIR to FBS_LIBRARY
comes from.

Now supported 5 variants: C++, Java, Python 2.x, Python 3.x and Go.
When PEERDIR comes from module for particular language appropriate variant
is selected.

**Notes:** FBS_NAMESPACE must be specified in all dependent FBS_LIBRARY modules
       if build of Go code is requested.

### Multimodule [JAVA_ANNOTATION_PROCESSOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L136) <a name="multimodule_JAVA_ANNOTATION_PROCESSOR"></a>

```ya.make
JAVA_ANNOTATION_PROCESSOR()
```

The module describing java annotation processor build.
Output artifacts: .jar and directory with all the jar to the classpath of the formation.

**Documentation:** https://wiki.yandex-team.ru/yatool/java/

### Multimodule [JAVA_CONTRIB_ANNOTATION_PROCESSOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L154) <a name="multimodule_JAVA_CONTRIB_ANNOTATION_PROCESSOR"></a>

_Not documented yet._

### Multimodule [JAVA_CONTRIB_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L418) <a name="multimodule_JAVA_CONTRIB_PROGRAM"></a>

_Not documented yet._

### Multimodule [JAVA_LIBRARY_SPLIT](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L16) <a name="multimodule_JAVA_LIBRARY_SPLIT"></a>

```ya.make
JAVA_LIBRARY_SPLIT()
```

The module describing java library build.
Split into full and interface jar submodules.

**Documentation:** https://wiki.yandex-team.ru/yatool/java/

### Multimodule [JAVA_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L85) <a name="multimodule_JAVA_PROGRAM"></a>

```ya.make
JAVA_PROGRAM()
```

The module describing java programs build.
Output artifacts: .jar and directory with all the jar to the classpath of the formation.

**Documentation:** https://wiki.yandex-team.ru/yatool/java/

### Multimodule [JTEST](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L317) <a name="multimodule_JTEST"></a>

_Not documented yet._

### Multimodule [JTEST_FOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L377) <a name="multimodule_JTEST_FOR"></a>

_Not documented yet._

### Multimodule [JUNIT5](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L255) <a name="multimodule_JUNIT5"></a>

_Not documented yet._

### Multimodule [JUNIT6](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L191) <a name="multimodule_JUNIT6"></a>

_Not documented yet._

### Multimodule [PACKAGE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2516) <a name="multimodule_PACKAGE"></a>

```ya.make
PACKAGE(name)
```

Module collects what is described directly inside it, builds and collects all its transitively available PEERDIRs.
As a result, build directory of the project gets the structure of the accessible part of Arcadia, where the build result of each PEERDIR is placed to relevant Arcadia subpath.
The data can be optionally packed if macro PACK() is used.

Is only used together with the macros FILES(), PEERDIR(), COPY(), FROM_SANDBOX(), RUN_PROGRAM or BUNDLE(). Don't use SRCS inside a PACKAGE.

**Documentation:** https://wiki.yandex-team.ru/yatool/large-data/

**See also:** [PACK()](#macro_PACK)

### Multimodule [PROTO_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L917) <a name="multimodule_PROTO_LIBRARY"></a>

```ya.make
PROTO_LIBRARY()
```

Build some varian of protocol buffers library.

The particular variant is selected based on where PEERDIR to PROTO_LIBRARY comes from.

Now supported 5 variants: C++, Java, Python 2.x, Python 3.x and Go.
When PEERDIR comes from module for particular language appropriate variant is selected.
PROTO_LIBRARY also supports emission of GRPC code if GRPC() macro is specified.
**Notes:**
- Python versions emit C++ code in addition to Python as optimization.
- In some PROTO_LIBRARY-es Java or Python versions are excluded via EXCLUDE_TAGS macros due to incompatibilities.
- Use from DEPENDS or BUNDLE is not allowed

**Documentation:** https://wiki.yandex-team.ru/yatool/proto_library/

**See also:** [GRPC()](#macro_GRPC), [OPTIMIZE_PY_PROTOS()](#macro_OPTIMIZE_PY_PROTOS), [INCLUDE_TAGS()](#macro_INCLUDE_TAGS), [EXCLUDE_TAGS()](#macro_EXCLUDE_TAGS)

### Multimodule [PROTO_SCHEMA](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1004) <a name="multimodule_PROTO_SCHEMA"></a>

```ya.make
PROTO_SCHEMA()
```

Build some variant of protocol buffers library or proto descriptions.

When used as a PEERDIR from a language module like GO_PROGRAM it behaves like PROTO_LIBRARY.
When built directly or by RECURSE it produces proto descriptions.
PROTO_SCHEMA can depend on PROTO_LIBRARY, but PROTO_LIBRARY cannot depend on PROTO_SCHEMA.

**See also:** [PROTO_LIBRARY()](#module_PROTO_LIBRARY)

### Multimodule [PY23_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1211) <a name="multimodule_PY23_LIBRARY"></a>

```ya.make
PY23_LIBRARY([name])
```

Build PY2_LIBRARY or PY3_LIBRARY depending on incoming PEERDIR.
Direct build or build by RECURSE creates both variants.
This multimodule doesn't define any final targets, so use from DEPENDS or BUNDLE is not allowed.

**Documentation:** https://wiki.yandex-team.ru/arcadia/python/pysrcs

### Multimodule [PY23_NATIVE_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1237) <a name="multimodule_PY23_NATIVE_LIBRARY"></a>

```ya.make
PY23_NATIVE_LIBRARY([name])
```

Build LIBRARY compatible with either Python 2.x or Python 3.x depending on incoming PEERDIR.

This multimodule doesn't depend on Arcadia Python binary build. It is intended only for C++ code and cannot contain PY_SRCS and USE_PYTHON2 macros.
Use these multimodule instead of PY23_LIBRARY if the C++ extension defined in it will be used in PY2MODULE.
While it doesn't bring Arcadia Python dependency itself, it is still compatible with Arcadia Python build and can be PEERDIR-ed from PY2_LIBRARY and alikes.
Proper version will be selected according to Python version of the module PEERDIR comes from.

This mulrtimodule doesn't define any final targets so cannot be used from DEPENDS or BUNDLE macros.

For more information read https://wiki.yandex-team.ru/arcadia/python/pysrcs/#pysrcssrcsipy23nativelibrary

**See also:** [LIBRARY()](#module_LIBRARY), [PY2MODULE()](#module_PY2MODULE)

### Multimodule [PY23_TEST](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1263) <a name="multimodule_PY23_TEST"></a>

_Not documented yet._

### Multimodule [PY3TEST](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L515) <a name="multimodule_PY3TEST"></a>

```ya.make
PY3TEST([name])
```

The test module for Python 3.x based on py.test

This module is compatible only with PYTHON3-tagged modules and selects peers from multimodules accordingly.
This module is only compatible with Arcadia Python build (to avoid tests duplication from Python2/3-tests). For non-Arcadia python use PYTEST.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#testynapytest
Documentation about the Arcadia test system: https://wiki.yandex-team.ru/yatool/test/

### Multimodule [PY3_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L347) <a name="multimodule_PY3_PROGRAM"></a>

```ya.make
PY3_PROGRAM([progname])
```

Python 3.x binary program. Links all Python 3.x libraries and Python 3.x interpreter into itself to form regular executable.
If name is not specified it will be generated from the name of the containing project directory.
This only compatible with PYTHON3-tagged modules and selects those from multimodules.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Multimodule [TS_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L39) <a name="multimodule_TS_LIBRARY"></a>

```ya.make
TS_LIBRARY([name])
```

The TypeScript/JavaScript library module, compiles TypeScript sources to JavaScript using tsc.
Build results are JavaScript files, typings and source mappings (depending on local tsconfig.json settings).

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_LIBRARY

**Example:**

```ya.make
TS_LIBRARY()

END()
```

### Multimodule [TS_NEXT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L74) <a name="multimodule_TS_NEXT"></a>

_Not documented yet._

### Multimodule [TS_PACKAGE](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_package.conf?rev=20020720#L13) <a name="multimodule_TS_PACKAGE"></a>

```ya.make
TS_PACKAGE()
```

The TypeScript/JavaScript library module, that does not need any compilation,
and is just a set of files and NPM dependencies. List required files in TS_FILES macro.
`package.json` is included by default.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_PACKAGE

### Multimodule [TS_RSPACK](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L49) <a name="multimodule_TS_RSPACK"></a>

_Not documented yet._

### Multimodule [TS_TEST_FOR](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L16) <a name="multimodule_TS_TEST_FOR"></a>

_Not documented yet._

### Multimodule [TS_TSC](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_tsc.conf?rev=20020720#L22) <a name="multimodule_TS_TSC"></a>

_Not documented yet._

### Multimodule [TS_VITE](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L58) <a name="multimodule_TS_VITE"></a>

_Not documented yet._

### Multimodule [TS_WEBPACK](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L56) <a name="multimodule_TS_WEBPACK"></a>

_Not documented yet._

### Multimodule [YQL_UDF](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L183) <a name="multimodule_YQL_UDF"></a>

```ya.make
YQL_UDF(name)
```

User-defined function for YQL

Multimodule which is YQL_UDF_MODULE when built directly or referred by BUNDLE and DEPENDS macros.
If used by PEERDIRs it is usual static LIBRARY with default YQL dependencies, allowing code reuse between UDFs.

**See also:** [YQL_UDF_MODULE()](#module_YQL_UDF_MODULE)

### Multimodule [YQL_UDF_CONTRIB](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L225) <a name="multimodule_YQL_UDF_CONTRIB"></a>

_Not documented yet._

### Multimodule [YQL_UDF_YDB](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L204) <a name="multimodule_YQL_UDF_YDB"></a>

_Not documented yet._

## Modules <a name="modules"></a>

### Module [BOOSTTEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1520) _(deprecated)_ <a name="module_BOOSTTEST"></a>

```ya.make
BOOSTTEST([name])
```

Test module based on boost/test/unit_test.hpp.
As with entire boost library usage of this technology is deprecated in Arcadia and restricted with configuration error in most of projects.
No new module of this type should be introduced unless it is explicitly approved by C++ committee.

### Module [BOOSTTEST_WITH_MAIN](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1542) _(deprecated)_ <a name="module_BOOSTTEST_WITH_MAIN"></a>

```ya.make
BOOSTTEST_WITH_MAIN([name])
```

Same as BOOSTTEST (see above), but comes with builtin int main(argc, argv) implementation

### Module [CI_GROUP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2545) <a name="module_CI_GROUP"></a>

```ya.make
CI_GROUP()
```

Module collects what is described directly inside it transitively by PEERDIRs.
No particular layout of built artifacts is implied. This module is needed primarily for CI dependency analysis and may not trigger builds at all.

Is only used together with the macro PEERDIR() and FILES(). Don't use SRCS inside CI_GROUP().

### Module [CUDA_DEVICE_LINK_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L132) <a name="module_CUDA_DEVICE_LINK_LIBRARY"></a>

```ya.make
CUDA_DEVICE_LINK_LIBRARY()
```

The LIBRARY() module with an additional step with CUDA device linking.
Use [NVCC_DEVICE_LINK](#macro_NVCC_DEVICE_LINK) macro to specify sources for device link.

### Module [DEFAULT_IOS_INTERFACE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5544) <a name="module_DEFAULT_IOS_INTERFACE"></a>

_Not documented yet._

### Module [DLL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2307) <a name="module_DLL"></a>

```ya.make
DLL(name major_ver [minor_ver] [EXPORTS symlist_file] [PREFIX prefix])
```

Dynamic library module definition.
1. major_ver and minor_ver must be integers.
2. EXPORTS allows you to explicitly specify the list of exported functions. This accepts 2 kind of files: .exports with <lang symbol> pairs and JSON-line .symlist files
3. PREFIX allows you to change the prefix of the output file (default DLL has the prefix "lib").

DLL cannot participate in linking to programs but can be used from Java or as final artifact (packaged and deployed).

### Module [DLL_TOOL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2324) <a name="module_DLL_TOOL"></a>

DLL_TOOL is a DLL that can be used as a LD_PRELOAD tool.

### Module [DOCS_HTML](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L236) <a name="module_DOCS_HTML"></a>

```ya.make
DOCS_HTML()
```

This module provides the HTML archive output from the DOCS multimodule. You have to call
DOCS_HTML_FROM macro to specify the path to the DOCS module at least once. If there are
several calls to DOCS_HTML_FROM macro the last one wins (that is the last call to
DOCS_HTML_FROM is considered and all others are ignored).

**See also:** [DOCS_HTML_FROM()](#macro_DOCS_HTML_FROM), [DOCS()](#module_DOCSS)

### Module [DOCS_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L88) <a name="module_DOCS_LIBRARY"></a>

_Not documented yet._

### Module [EXECTEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1816) <a name="module_EXECTEST"></a>

```ya.make
EXECTEST()
```

Module definition of generic test that executes a binary.
Use macro RUN to specify binary to run.

**example:**

    EXECTEST()
        RUN(
            cat input.txt
        )
        DATA(
            arcadia/devtools/ya/test/tests/exectest/data
        )
        DEPENDS(
            devtools/dummy_arcadia/cat
        )
        TEST_CWD(devtools/ya/test/tests/exectest/data)
    END()

More examples: https://wiki.yandex-team.ru/yatool/test/#exec-testy

**See also:** [RUN()](#macro_RUN)

### Module [FAT_OBJECT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2105) <a name="module_FAT_OBJECT"></a>

```ya.make
FAT_OBJECT()
```

The "fat" object module. It will contain all its transitive dependencies reachable by PEERDIRs:
static libraries, local (from own SRCS) and global (from peers') object files.

Designed for use in XCode projects for iOS.

### Module [FUZZ](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1476) <a name="module_FUZZ"></a>

```ya.make
FUZZ()
```

In order to start using Fuzzing in Arcadia, you need to create a FUZZ module with the implementation of the function LLVMFuzzerTestOneInput().
This module should be reachable by RECURSE from /autocheck project in order for the corpus to be regularly updated.
AFL and Libfuzzer are supported in Arcadia via a single interface, but the automatic fuzzing still works only through Libfuzzer.

**Example:** https://github.com/yandex/yatool/tree/main/contrib/libs/re2/re2/fuzzing/re2_fuzzer.cc?rev=2919463#L58

**Documentation:** https://wiki.yandex-team.ru/yatool/fuzzing/

### Module [GEN_LIBRARY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L594) <a name="module_GEN_LIBRARY"></a>

```ya.make
GEN_LIBRARY()
```

Definition of a module that brings generated artefacts. This module can PEERDIRed
from any module. The resulted module is empty and cleaned up during construction
of the build graph.

NOTE! SRCS macro is not supported for this library.

### Module [GO_DLL](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1119) <a name="module_GO_DLL"></a>

```ya.make
GO_DLL(name major_ver [minor_ver] [PREFIX prefix])
```

Go ishared object module definition.
Compile and link Go module to a shared object.
Will select Go implementation on PEERDIR to PROTO_LIBRARY.

### Module [GO_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L993) <a name="module_GO_LIBRARY"></a>

```ya.make
GO_LIBRARY([name])
```

Go library module definition.
Compile Go module as a library suitable for PEERDIR from other Go modules.
Will select Go implementation on PEERDIR to PROTO_LIBRARY.

### Module [GO_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1010) <a name="module_GO_PROGRAM"></a>

```ya.make
GO_PROGRAM([name])
```

Go program module definition.
Compile and link Go module to an executable program.
Will select Go implementation on PEERDIR to PROTO_LIBRARY.

### Module [GO_TEST](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1139) <a name="module_GO_TEST"></a>

```ya.make
GO_TEST([name])
```

Go test module definition.
Compile and link Go module as a test suitable for running with Arcadia testing support.
All usual testing support macros like DATA, DEPENDS, SIZE, REQUIREMENTS etc. are supported.
Will select Go implementation on PEERDIR to PROTO_LIBRARY.

### Module [GTEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1452) <a name="module_GTEST"></a>

```ya.make
GTEST([name])
```

Unit test module based on library/cpp/testing/gtest.
It is recommended not to specify the name.

**Documentation:** https://docs.yandex-team.ru/arcadia-cpp/docs/build/manual/tests/cpp#gtest

### Module [G_BENCHMARK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1854) <a name="module_G_BENCHMARK"></a>

```ya.make
G_BENCHMARK([benchmarkname])
```

Benchmark test based on the google benchmark.

For more details see: https://github.com/yandex/yatool/tree/main/contrib/libs/benchmark/README.md

### Module [IOS_INTERFACE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5533) <a name="module_IOS_INTERFACE"></a>

```ya.make
IOS_INTERFACE()
```

iOS GUI module definition

### Module [JAVA_CONTRIB](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L765) <a name="module_JAVA_CONTRIB"></a>

_Not documented yet._

### Module [JAVA_CONTRIB_PROXY](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L709) <a name="module_JAVA_CONTRIB_PROXY"></a>

_Not documented yet._

### Module [JAVA_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L40) <a name="module_JAVA_LIBRARY"></a>

```ya.make
JAVA_LIBRARY()
```

The module describing java library build.

**Documentation:** https://wiki.yandex-team.ru/yatool/java/

### Module [JAVA_TEST_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L44) <a name="module_JAVA_TEST_LIBRARY"></a>

_Not documented yet._

### Module [LIBRARY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1989) <a name="module_LIBRARY"></a>

```ya.make
LIBRARY()
```

The regular static library module.

The LIBRARY() is intermediate module, so when built directly it won't build its dependencies.
It transitively provides its PEERDIRs to ultimate final target, where all LIBRARY() modules are built and linked together.

This is C++ library, and it selects peers from multimodules accordingly.

It makes little sense to mention LIBRARY in DEPENDS or BUNDLE, package and deploy it since it is not a standalone entity.
In order to use library in tests PEERDIR it to link into tests.
If you think you need to distribute static library please contact devtools@ for assistance.

### Module [PROGRAM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1303) <a name="module_PROGRAM"></a>

```ya.make
PROGRAM([progname])
```

Regular program module.
If name is not specified it will be generated from the name of the containing project directory.

### Module [PROTO_DESCRIPTIONS](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L976) <a name="module_PROTO_DESCRIPTIONS"></a>

_Not documented yet._

### Module [PROTO_REGISTRY](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L989) <a name="module_PROTO_REGISTRY"></a>

_Not documented yet._

### Module [PY2MODULE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L602) <a name="module_PY2MODULE"></a>

```ya.make
PY2MODULE(name major_ver [minor_ver] [EXPORTS symlist_file] [PREFIX prefix])
```

The Python external module for Python2 and any system Python
1. major_ver and minor_ver must be integers.
2. The resulting .so will have the prefix "lib".
3. Processing EXPORTS and PREFIX is the same as for DLL module
This is native DLL, so it will select C++ version from PROTO_LIBRARY.

**Note:** this module will always PEERDIR Python2 version of PY23_NATIVE_LIBRARY.
Do not PEERDIR PY2_LIBRARY or PY23_LIBRARY: this will link Python in and render artifact unusable as Python module.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Module [PY2TEST](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L463) <a name="module_PY2TEST"></a>

```ya.make
PY2TEST([name])
```

The test module for Python 2.x based on py.test

This module is compatible only with PYTHON2-tagged modules and selects peers from multimodules accordingly.
This module is compatible with non-Arcadia Python builds.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#python
Documentation about the Arcadia test system: https://wiki.yandex-team.ru/yatool/test/

### Module [PY2_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L694) _(deprecated)_ <a name="module_PY2_LIBRARY"></a>

```ya.make
PY2_LIBRARY()
```

Deprecated. Use PY23_LIBRARY or PY3_LIBRARY instead.
Python 2.x binary built library. Builds sources from PY_SRCS to data suitable for PY2_PROGRAM.
Adds dependencies to Python 2.x runtime library from Arcadia.
This module is only compatible with PYTHON2-tagged modules and selects those from multimodules.
This module is only compatible with Arcadia Python build.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Module [PY2_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L315) _(deprecated)_ <a name="module_PY2_PROGRAM"></a>

```ya.make
PY2_PROGRAM([progname])
```

Deprecated. Use PY3_PROGRAM instead.
Python 2.x binary program. Links all Python 2.x libraries and Python 2.x interpreter into itself to form regular executable.
If name is not specified it will be generated from the name of the containing project directory.
This only compatible with PYTHON2-tagged modules and selects those from multimodules.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Module [PY3MODULE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L621) <a name="module_PY3MODULE"></a>

```ya.make
PY3MODULE(name major_ver [minor_ver] [EXPORTS symlist_file] [PREFIX prefix])
```

The Python external module for Python3 and any system Python
1. major_ver and minor_ver must be integers.
2. The resulting .so will have the prefix "lib".
3. Processing EXPORTS and PREFIX is the same as for DLL module
This is native DLL, so it will select C++ version from PROTO_LIBRARY.

**Note:** this module will always PEERDIR Python3 version of PY23_NATIVE_LIBRARY.
Do not PEERDIR PY3_LIBRARY or PY23_LIBRARY: this will link Python in and render artifact unusable as Python module.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Module [PY3TEST_BIN](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L486) _(deprecated)_ <a name="module_PY3TEST_BIN"></a>

```ya.make
PY3TEST_BIN()
```

Same as PY3TEST. Don't use this, use PY3TEST instead.

### Module [PY3_LIBRARY](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L738) <a name="module_PY3_LIBRARY"></a>

```ya.make
PY3_LIBRARY()
```

Python 3.x binary library. Builds sources from PY_SRCS to data suitable for PY2_PROGRAM
Adds dependencies to Python 2.x runtime library from Arcadia.
This module is only compatible with PYTHON3-tagged modules and selects those from multimodules.
This module is only compatible with Arcadia Python build.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Module [PY3_PROGRAM_BIN](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L915) <a name="module_PY3_PROGRAM_BIN"></a>

```ya.make
PY3_PROGRAM_BIN([progname])
```

Use instead of PY3_PROGRAM only if ya.make with PY3_PROGRAM() included in another ya.make
In all other cases use PY3_PROGRAM

### Module [PYTEST_BIN](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L445) _(deprecated)_ <a name="module_PYTEST_BIN"></a>

```ya.make
PYTEST_BIN()
```

Same as PY2TEST. Don't use this, use PY2TEST instead.

### Module [PY_ANY_MODULE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L544) <a name="module_PY_ANY_MODULE"></a>

```ya.make
PY_ANY_MODULE(name major_ver [minor_ver] [EXPORTS symlist_file] [PREFIX prefix])
```

The Python external module for any versio of Arcadia or system Python.
1. major_ver and minor_ver must be integers.
2. The resulting .so will have the prefix "lib".
3. Processing EXPORTS and PREFIX is the same as for DLL module
This is native DLL, so it will select C++ version from PROTO_LIBRARY.

**Note:** Use PYTHON2_MODULE()/PYTHON3_MODULE() in order to PEERDIR proper version of PY23_NATIVE_LIBRARY.
Do not PEERDIR any PY*_LIBRARY: this will link Python in and render artifact unusable as Python module.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs/

### Module [RECURSIVE_LIBRARY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2164) <a name="module_RECURSIVE_LIBRARY"></a>

```ya.make
RECURSIVE_LIBRARY()
```

The recursive ("fat") library module. It will contain all its transitive dependencies reachable by PEERDIRs:
from static libraries, local (from own SRCS) and global (from peers') object files.

Designed for use in XCode projects for iOS.

### Module [RESOURCES_LIBRARY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2064) <a name="module_RESOURCES_LIBRARY"></a>

```ya.make
RESOURCES_LIBRARY()
```

Definition of a module that brings its content from external source (Sandbox) via DECLARE_EXTERNAL_RESOURCE macro.
This can participate in PEERDIRs of others as library but it cannot have own sources and PEERDIRs.

**See also:** [DECLARE_EXTERNAL_RESOURCE()](#macro_DECLARE_EXTERNAL_RESOURCE)

### Module [R_MODULE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2277) <a name="module_R_MODULE"></a>

```ya.make
R_MODULE(name major_ver [minor_ver] [EXPORTS symlist_file] [PREFIX prefix])
```

The external module for R language.
1. major_ver and minor_ver must be integers.
2. The resulting .so will have the prefix "lib".
3. Processing EXPORTS and PREFIX is the same as for DLL module
This is native DLL, so it will select C++ version from PROTO_LIBRARY.

### Module [SO_PROGRAM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2336) <a name="module_SO_PROGRAM"></a>

```ya.make
SO_PROGRAM(name major_ver [minor_ver] [EXPORTS symlist_file] [PREFIX prefix])
```

Executable dynamic library module definition.
1. major_ver and minor_ver must be integers.
2. EXPORTS allows you to explicitly specify the list of exported functions. This accepts 2 kind of files: .exports with <lang symbol> pairs and JSON-line .symlist files
3. PREFIX allows you to change the prefix of the output file.

### Module [TS_TEST_HERMIONE_FOR](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L95) <a name="module_TS_TEST_HERMIONE_FOR"></a>

_Not documented yet._

### Module [TS_TEST_JEST_FOR](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L30) <a name="module_TS_TEST_JEST_FOR"></a>

_Not documented yet._

### Module [TS_TEST_PLAYWRIGHT_FOR](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L129) <a name="module_TS_TEST_PLAYWRIGHT_FOR"></a>

_Not documented yet._

### Module [TS_TEST_PLAYWRIGHT_LARGE_FOR](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L160) <a name="module_TS_TEST_PLAYWRIGHT_LARGE_FOR"></a>

_Not documented yet._

### Module [TS_TEST_VITEST_FOR](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L63) <a name="module_TS_TEST_VITEST_FOR"></a>

_Not documented yet._

### Module [UNION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2567) <a name="module_UNION"></a>

```ya.make
UNION(name)
```

Collection of PEERDIR dependencies, files and artifacts.
UNION doesn't build its peers, just provides those to modules depending on it.
When specified in DEPENDS() macro the UNION is transitively closed, building all its peers and providing those by own paths (without adding this module path like PACKAGE does).

Is only used together with the macros like FILES(), PEERDIR(), COPY(), FROM_SANDBOX(), RUN_PROGRAM or BUNDLE(). Don't use SRCS inside a UNION.

**Documentation:** https://wiki.yandex-team.ru/yatool/large-data/

### Module [UNITTEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1397) <a name="module_UNITTEST"></a>

```ya.make
UNITTEST([name])
```

Unit test module based on library/cpp/testing/unittest.
It is recommended not to specify the name.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#opisanievya.make1

### Module [UNITTEST_FOR](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1898) <a name="module_UNITTEST_FOR"></a>

```ya.make
UNITTEST_FOR(path/to/lib)
```

Convenience extension of UNITTEST module.
The UNINTTEST module with additional SRCDIR + ADDINCL + PEERDIR on path/to/lib.
path/to/lib is the path to the directory with the LIBRARY project.

Documentation about the Arcadia test system: https://wiki.yandex-team.ru/yatool/test/

### Module [UNITTEST_WITH_CUSTOM_ENTRY_POINT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1433) <a name="module_UNITTEST_WITH_CUSTOM_ENTRY_POINT"></a>

```ya.make
UNITTEST_WITH_CUSTOM_ENTRY_POINT([name])
```

Generic unit test module.

### Module [YQL_PYTHON3_UDF](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L326) <a name="module_YQL_PYTHON3_UDF"></a>

```ya.make
YQL_PYTHON3_UDF(name)
```

The extension module for YQL with Python 3.x UDF (User Defined Function for YQL).
Unlike YQL_UDF this is plain DLL module, so PEERDIRs to it are not allowed.

**Documentation:** https://yql.yandex-team.ru/docs/yt/udf/python/

### Module [YQL_PYTHON3_UDF_TEST](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L377) <a name="module_YQL_PYTHON3_UDF_TEST"></a>

```ya.make
YQL_PYTHON3_UDF_TEST(name)
```

The Python test for Python 3.x YQL UDF (User Defined Function for YQL). The code should be a proper YQL_PYTHON3_UDF.

This module will basically build itself as UDF and run as test using yql/tools/run_python_udf/run_python_udf tool.

**Documentation:** https://yql.yandex-team.ru/docs/yt/udf/python/

**See also:** [YQL_PYTHON3_UDF()](#module_YQL_PYTHON3_UDF)

### Module [YQL_PYTHON_UDF](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L269) <a name="module_YQL_PYTHON_UDF"></a>

```ya.make
YQL_PYTHON_UDF(name)
```

Definition of the extension module for YQL with Python 2.x UDF (User Defined Function for YQL).
Unlike YQL_UDF this is plain DLL module, so PEERDIRs to it are not allowed.

https://yql.yandex-team.ru/docs/yt/udf/python/

### Module [YQL_PYTHON_UDF_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L298) <a name="module_YQL_PYTHON_UDF_PROGRAM"></a>

```ya.make
YQL_PYTHON_UDF_PROGRAM(name)
```

Definition of the extension module for YQL with Python 2.x UDF (User Defined Function for YQL).
Unlike YQL_UDF this is plain DLL module, so PEERDIRs to it are not allowed.

https://yql.yandex-team.ru/docs/yt/udf/python/

### Module [YQL_PYTHON_UDF_TEST](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L363) <a name="module_YQL_PYTHON_UDF_TEST"></a>

```ya.make
YQL_PYTHON_UDF_TEST(name)
```

The Python test for Python YQL UDF (Python User Defined Function for YQL). The code should be a proper YQL_PYTHON_UDF.

This module will basically build itself as UDF and run as test using yql/tools/run_python_udf/run_python_udf tool.

**Documentation:** https://yql.yandex-team.ru/docs/yt/udf/python/

**example:** https://github.com/yandex/yatool/tree/main/yql/udfs/test/simple/ya.make

**See also:** [YQL_PYTHON_UDF()](#module_YQL_PYTHON_UDF)

### Module [YQL_UDF_MINITEST](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L65) <a name="module_YQL_UDF_MINITEST"></a>

```ya.make
YQL_UDF_MINITEST([name])
```

The module to test YQL C++ UDF via minirun (pure provider, no external data sources).
Test SQL files use inline data (AsList/AS_TABLE) for input instead of .in files.
Tests with 'forceblocks' in .cfg are additionally run in Blocks and Peephole modes.

### Module [YQL_UDF_MODULE](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L150) <a name="module_YQL_UDF_MODULE"></a>

```ya.make
YQL_UDF_MODULE(name)
```

The extension module for YQL with C++ UDF (User Defined Function YQL)

https://yql.yandex-team.ru/docs/yt/udf/cpp/

### Module [YQL_UDF_MODULE_CONTRIB](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L162) <a name="module_YQL_UDF_MODULE_CONTRIB"></a>

_Not documented yet._

### Module [YQL_UDF_TEST](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L43) <a name="module_YQL_UDF_TEST"></a>

```ya.make
YQL_UDF_TEST([name])
```

The module to test YQL C++ UDF.

**Documentation:** https://yql.yandex-team.ru/docs/yt/libraries/testing/
Documentation about the Arcadia test system: https://wiki.yandex-team.ru/yatool/test/

### Module [YQL_UDF_YDB_MODULE](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L156) <a name="module_YQL_UDF_YDB_MODULE"></a>

_Not documented yet._

### Module [YT_UNITTEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1423) <a name="module_YT_UNITTEST"></a>

```ya.make
YT_UNITTEST([name])
```

YT Unit test module based on library/cpp/testing/unittest with NYT::Initialize hook

### Module [Y_BENCHMARK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1836) <a name="module_Y_BENCHMARK"></a>

```ya.make
Y_BENCHMARK([benchmarkname])
```

Benchmark test based on the library/cpp/testing/benchmark.

For more details see: https://wiki.yandex-team.ru/yatool/test/#zapuskbenchmark

## Macros <a name="macros"></a>

### Macro [ACCELEO](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L5) <a name="macro_ACCELEO"></a>

```ya.make
ACCELEO(XSD{input}[], MTL{input}[], MTL_ROOT="${MODDIR}", LANG{input}[], OUT{output}[], OUT_NOAUTO{output}[], OUTPUT_INCLUDES[], DEBUG?"stdout2stderr":"stderr2stdout")
```

_Not documented yet._

### Macro [ADDINCL](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_ADDINCL"></a>

```ya.make
ADDINCL([FOR <lang>][GLOBAL dir]* dirlist)  # builtin
```

The macro adds the directories to include/import search path to compilation flags of the current project.
By default settings apply to C/C++ compilation namely sets -I<library path> flag, use FOR argument to change target command.
**params:**
`FOR <lang>` - adds includes/import search path for other language. E.g. `FOR proto` adds import search path for .proto files processing.
`GLOBAL` - extends the search for headers (-I) on the dependent projects.

### Macro [ADDINCLSELF](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3179) <a name="macro_ADDINCLSELF"></a>

```ya.make
ADDINCLSELF()
```

The macro adds the -I<project source path> flag to the source compilation flags of the current project.

### Macro [ADD_CHECK](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L772) <a name="macro_ADD_CHECK"></a>

_Not documented yet._

### Macro [ADD_CHECK_PY_IMPORTS](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) <a name="macro_ADD_CHECK_PY_IMPORTS"></a>

_Not documented yet._

### Macro [ADD_CLANG_TIDY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1185) <a name="macro_ADD_CLANG_TIDY"></a>

```ya.make
ADD_CLANG_TIDY()
```

_Not documented yet._

### Macro [ADD_COMPILABLE_TRANSLATE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2774) <a name="macro_ADD_COMPILABLE_TRANSLATE"></a>

```ya.make
ADD_COMPILABLE_TRANSLATE(Dict Name Options...)
```

Generate translation dictionary code to transdict.LOWER(Name).cpp that will than be compiled into library

### Macro [ADD_COMPILABLE_TRANSLIT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2784) <a name="macro_ADD_COMPILABLE_TRANSLIT"></a>

```ya.make
ADD_COMPILABLE_TRANSLIT(TranslitTable NGrams Name Options...)
```

Generate transliteration dictionary code
This will emit both translit, untranslit and ngrams table codes those will be than further compiled into library

### Macro [ADD_DLLS_TO_JAR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2149) <a name="macro_ADD_DLLS_TO_JAR"></a>

```ya.make
ADD_DLLS_TO_JAR()
```

_Not documented yet._

### Macro [ADD_IWYU](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1197) <a name="macro_ADD_IWYU"></a>

```ya.make
ADD_IWYU()
```

_Not documented yet._

### Macro [ADD_PYTEST_BIN](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) <a name="macro_ADD_PYTEST_BIN"></a>

_Not documented yet._

### Macro [ADD_YTEST](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L1567) <a name="macro_ADD_YTEST"></a>

_Not documented yet._

### Macro [ALICE_GENERATE_FUNCTION_PROTO_INCLUDES](https://a.yandex-team.ru/arcadia/build/internal/plugins/alice.py?rev=20020720#L94) <a name="macro_ALICE_GENERATE_FUNCTION_PROTO_INCLUDES"></a>

Generates proto_includes.h file that includes all function proto headers and add descriptors to generated_pool
Is used to register them in protobuf descriptor generated pool without enumerating each file manually

### Macro [ALICE_GENERATE_FUNCTION_SPECS](https://a.yandex-team.ru/arcadia/build/internal/plugins/alice.py?rev=20020720#L47) <a name="macro_ALICE_GENERATE_FUNCTION_SPECS"></a>

```ya.make
ALICE_GENERATE_FUNCTION_SPECS([DONT_ADD_TO_RESOURCE])
```

Generates Llm functions specs from alice/functions/proto ,
Puts it in ${BINDIR}/llm_function_specs.json , ${BINDIR}/llm_function_specs_en-GB.json
Also adds it into resources

Use DONT_ADD_TO_RESOURCE if you dont need it in resources

### Macro [ALLOCATOR](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2672) <a name="macro_ALLOCATOR"></a>

```ya.make
ALLOCATOR(Alloc)
```

Set memory allocator implementation for the PROGRAM()/DLL() module.
This may only be specified for programs and dlls, use in other modules leads to configuration errors.

Different platforms have different default allocators (for example, Linux x86-64 uses TCMALLOC_TC for builds w/o MUSL, Darwin uses SYSTEM).
See DEFAULT_ALLOCATOR variable definition.

Available allocators are:
  - LF - lfalloc (https://github.com/yandex/yatool/tree/main/library/cpp/lfalloc)
  - LF_YT - Allocator selection for YT (https://github.com/yandex/yatool/tree/main/library/cpp/lfalloc/yt/ya.make)
  - LF_DBG - Debug allocator selection (https://github.com/yandex/yatool/tree/main/library/cpp/lfalloc/dbg/ya.make)
  - YT - The YTAlloc allocator (https://github.com/yandex/yatool/tree/main/library/cpp/ytalloc/impl/ya.make)
  - J - The JEMalloc allocator (https://github.com/yandex/yatool/tree/main/library/malloc/jemalloc)
  - B - The balloc allocator by Pyotr Popov and Anton Samokhvalov
      - Discussion: https://at.yandex-team.ru/users/ironpeter/126
      - Code: https://github.com/yandex/yatool/tree/main/library/cpp/balloc
  - BM - The balloc for market (agri@ commits from july 2018 till November 2018 saved)
  - C - Like B, but can be disabled for each thread to LF or SYSTEM one (B can be disabled only to SYSTEM)
  - MIM - Microsoft's mimalloc (actual version) (https://github.com/yandex/yatool/tree/main/library/malloc/mimalloc)
  - MIM_SDC - Microsoft's mimalloc patched by SDC (https://github.com/yandex/yatool/tree/main/library/malloc/mimalloc_sdc)
  - TCMALLOC - Google TCMalloc (actual version) (https://github.com/yandex/yatool/tree/main/library/malloc/tcmalloc)
  - TCMALLOC_256K - TCMalloc with 256k pages (usually faster but fragmentation is higher) + huge page awareness (https://github.com/yandex/yatool/tree/main/contrib/libs/tcmalloc/default)
  - TCMALLOC_SMALL_BUT_SLOW (https://github.com/yandex/yatool/tree/main/contrib/libs/tcmalloc/small_but_slow)
       TCMalloc small-but-slow is a a version of TCMalloc that chooses to minimize
       fragmentation at a *severe* cost to performance.  It should be used by
       applications that have significant memory constraints, but don't need to
       frequently allocate/free objects.
  - TCMALLOC_NUMA_256K - TCMalloc with 256k pages (usually faster but fragmentation is higher) + NUMA awareness (https://github.com/yandex/yatool/tree/main/contrib/libs/tcmalloc/numa_256k)
  - TCMALLOC_NUMA_LARGE_PAGES - TCMalloc with large pages (usually faster but fragmentation is higher) + NUMA awareness (https://github.com/yandex/yatool/tree/main/contrib/libs/tcmalloc/numa_large_pages)
  - TCMALLOC_TC - TCMalloc with 256k pages (usually faster but fragmentation is higher) + huge page awareness + Per-thread mode (https://github.com/yandex/yatool/tree/main/contrib/libs/tcmalloc/no_percpu_cache)
  - GOOGLE - Google TCMalloc (old version) (https://github.com/yandex/yatool/tree/main/library/malloc/galloc)
  - LOCKLESS - Allocator based upon lockless queues (https://github.com/yandex/yatool/tree/main/library/malloc/lockless)
  - HU - Huge page allocator by @gulin.
      - Discussion: https://at.yandex-team.ru/users/gulin/759
      - Code: https://a.yandex-team.ru/arcadia/library/cpp/malloc/hu
  - PROFILED_HU - patched HU (https://a.yandex-team.ru/arcadia/library/cpp/malloc/profiled_hu). It is a bit slower but has metrics of memory consumption.
  - THREAD_PROFILED_HU - patched (special for market) HU (https://a.yandex-team.ru/arcadia/library/cpp/malloc/thread_profiled_hu). It is a bit slower but has metrics of memory consumption.
  - SYSTEM - Use target system allocator
  - FAKE - Don't link with any allocator

More about allocators in Arcadia (outdated): https://wiki.yandex-team.ru/arcadia/allocators/

### Macro [ALLOCATOR_IMPL](https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L113) <a name="macro_ALLOCATOR_IMPL"></a>

```ya.make
ALLOCATOR_IMPL()
```

_Not documented yet._

### Macro [ALL_GO_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L410) <a name="macro_ALL_GO_SRCS"></a>

```ya.make
ALL_GO_SRCS()
```

Puts all non-test .go files from the project's directory into SRCS of the current module.

**Note:** Only one such macro per module is allowed.
**Note:** Lookup is non-recursive: nested directories are separate Go packages and must have their own ya.make.

**See also:** [SRCS()](#macro_SRCS)

### Macro [ALL_PYTEST_SRCS](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1169) <a name="macro_ALL_PYTEST_SRCS"></a>

```ya.make
ALL_PYTEST_SRCS([RECURSIVE] [Dirs...])
```

Puts all .py-files from given Dirs (relative to projects') into TEST_SRCS of the current module.
If Dirs is omitted project directory is used

`RECURSIVE` makes lookup recursive with respect to Dirs
`ONLY_TEST_FILES` includes only files `test_*.py` and `*_test.py`, others are normally subject to `PY_SRCS`

**Note:** Only one such macro per module is allowed
**Note:** Macro is designed to reject any ya.make files in Dirs except current one

**See also:** [TEST_SRCS()](#macro_TEST_SRCS)

### Macro [ALL_PY_EXTRA_LINT_FILES](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1189) <a name="macro_ALL_PY_EXTRA_LINT_FILES"></a>

```ya.make
ALL_PY_EXTRA_LINT_FILES([Dirs...])
```

Add all Python .py-files from current directory for linting. Normally the files are added by
PY_SRCS / TEST_SRCS macros. This macro can be used when source files' layout causes linting
errors related to unsorted imports (relative imports of files which don't belong to the current module).

### Macro [ALL_PY_SRCS](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1151) <a name="macro_ALL_PY_SRCS"></a>

```ya.make
ALL_PY_SRCS([RECURSIVE] [NO_TEST_FILES] { | TOP_LEVEL | NAMESPACE ns} [Dirs...])
```

Puts all .py-files from given Dirs (relative to projects') into PY_SRCS of the current module.
If Dirs is ommitted project directory is used

`RECURSIVE` makes lookup recursive with resprect to Dirs
`NO_TEST_FILES` excludes files `test_*.py` and `*_test.py` those are normally subject to `TEST_SRCS`
`TOP_LEVEL` and `NAMESPACE` are forwarded to `PY_SRCS`

**Note:** Only one such macro per module is allowed
**Note:** Macro is designed to reject any ya.make files in Dirs except current one

**See also:** [PY_SRCS()](#macro_PY_SRCS)

### Macro [ALL_RESOURCE_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2863) <a name="macro_ALL_RESOURCE_FILES"></a>

```ya.make
ALL_RESOURCE_FILES(Ext [PREFIX {prefix}] [STRIP {strip}] Dirs...)
```

This macro collects all files with extension `Ext` and
Passes them to `RESOURCE_FILES` macro as relative to current directory

`PREFIX` and `STRIP` have the same meaning as in `RESOURCE_FILES`, both are applied over moddir-relative paths

**Note:** This macro can be used multiple times per ya.make, but only once for each Ext value
**Note:** Wildcards are not allowed neither as Ext nor in Dirs

### Macro [ALL_RESOURCE_FILES_FROM_DIRS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2878) <a name="macro_ALL_RESOURCE_FILES_FROM_DIRS"></a>

```ya.make
ALL_RESOURCE_FILES_FROM_DIRS([PREFIX {prefix}] [STRIP {strip}] Dirs...)
```

This macro collects all files non-recursively from listed Dirs and
Passes them to `RESOURCE_FILES` macro as relative to current directory
The macro is usefull if literally all files are needed because `ALL_RESOURCE_FILES` requires extension to be specified

`PREFIX` and `STRIP` have the same meaning as in `RESOURCE_FILES`, both are applied over moddir-relative paths

**Note:** This macro can be used only once per ya.make
**Note:** Wildcards are not allowed neither as Ext nor in Dirs

### Macro [ALL_SRCS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2454) <a name="macro_ALL_SRCS"></a>

```ya.make
ALL_SRCS([GLOBAL] filenames...)
```

Make all source files listed as GLOBAL or not depending on the keyword GLOBAL
Call to ALL_SRCS macro is equivalent to call to GLOBAL_SRCS macro when GLOBAL keyword is specified
as the first argument and is equivalent to call to SRCS macro otherwise.

**example:**

    LIBRARY()
        SET(MAKE_IT_GLOBAL GLOBAL)
        ALL_SRCS(${MAKE_IT_GLOBAL} foo.cpp bar.cpp)
    END()

**See also:** [GLOBAL_SRCS()](#macro_GLOBAL_SRCS), [SRCS()](#macro_SRCS)

### Macro [ANNOTATION_PROCESSOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2083) <a name="macro_ANNOTATION_PROCESSOR"></a>

```ya.make
ANNOTATION_PROCESSOR(processors...)
```

The macro is in development.
Used to specify annotation processors to build JAVA_PROGRAM() and JAVA_LIBRARY().

### Macro [ARCHIVE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4163) <a name="macro_ARCHIVE"></a>

```ya.make
ARCHIVE(archive_name [DONT_COMPRESS] files...)
```

Add arbitrary data to a modules. Unlike RESOURCE macro the result should be futher processed by othet macros in the module.

**Example:** https://wiki.yandex-team.ru/yatool/howtowriteyamakefiles/#a1ispolzujjtekomanduarchive

### Macro [ARCHIVE_ASM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4141) <a name="macro_ARCHIVE_ASM"></a>

```ya.make
ARCHIVE_ASM(NAME archive_name files...)
```

Similar to the macro ARCHIVE, but:
1. works faster and it is better to use for large files.
2. Different syntax (see examples in codesearch or users/pg/tests/archive_test)

### Macro [ARCHIVE_BY_KEYS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4174) <a name="macro_ARCHIVE_BY_KEYS"></a>

```ya.make
ARCHIVE_BY_KEYS(archive_name key [DONT_COMPRESS] files...)
```

Add arbitrary data to a module be accessible by specified key.
Unlike RESOURCE macro the result should be futher processed by othet macros in the module.

**Example:** https://wiki.yandex-team.ru/yatool/howtowriteyamakefiles/#a1ispolzujjtekomanduarchive

### Macro [AR_PLUGIN](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3417) <a name="macro_AR_PLUGIN"></a>

```ya.make
AR_PLUGIN(plugin_name)
```

Register script, which will process module's .a (archive) output
Script will receive path to archive, which it should modify in place

### Macro [ASM_PREINCLUDE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5240) <a name="macro_ASM_PREINCLUDE"></a>

```ya.make
ASM_PREINCLUDE(AsmFiles...)
```

Supply additional .asm files to all assembler calls within a module

### Macro [ASSERT](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L30) <a name="macro_ASSERT"></a>

_Not documented yet._

### Macro [AUTO_SERVICE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L122) <a name="macro_AUTO_SERVICE"></a>

```ya.make
AUTO_SERVICE(Ver)
```

_Not documented yet._

### Macro [BENCHMARK_OPTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1873) <a name="macro_BENCHMARK_OPTS"></a>

```ya.make
BENCHMARK_OPTS(opt1 [opt2...])
```

Allows to specify extra args to benchmark binary.
Supported for G_BENCHMARK and Y_BENCHMARK

**example:**
    BENCHMARK_OPTS (
        --benchmark_min_time=0
    )

**Documentation:** https://docs.yandex-team.ru/ya-make/manual/tests/benchmark

### Macro [BISON_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L57) <a name="macro_BISON_FLAGS"></a>

```ya.make
BISON_FLAGS(<flags>)
```

Set flags for Bison tool invocations.

### Macro [BISON_GEN_C](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L64) <a name="macro_BISON_GEN_C"></a>

```ya.make
BISON_GEN_C()
```

Generate C from Bison grammar. The C++ is generated by default.

### Macro [BISON_GEN_CPP](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L72) <a name="macro_BISON_GEN_CPP"></a>

```ya.make
BISON_GEN_CPP()
```

Generate C++ from Bison grammar. This is current default.

### Macro [BISON_HEADER](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L94) <a name="macro_BISON_HEADER"></a>

```ya.make
BISON_HEADER(<header_suffix>)
```

Use SUFF (including extension) to name Bison defines header file. The default is just `.h`.

### Macro [BISON_NO_HEADER](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L104) <a name="macro_BISON_NO_HEADER"></a>

```ya.make
BISON_NO_HEADER()
```

Don't emit Bison defines header file.

### Macro [BPF](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5009) <a name="macro_BPF"></a>

```ya.make
BPF(Input Output Opts...)
```

Emit eBPF bytecode from .c file.
**Note:** Output name is used as is, no extension added.

### Macro [BPF_STATIC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5023) <a name="macro_BPF_STATIC"></a>

```ya.make
BPF_STATIC(Input Output Opts...)
```

Emit eBPF bytecode from .c file.
**Note:** Output name is used as is, no extension added.

### Macro [BUILDWITH_CYTHON_C](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4047) <a name="macro_BUILDWITH_CYTHON_C"></a>

```ya.make
BUILDWITH_CYTHON_C(Src Options...)
```

Generates .c file from .pyx.

### Macro [BUILDWITH_CYTHON_CPP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4016) <a name="macro_BUILDWITH_CYTHON_CPP"></a>

```ya.make
BUILDWITH_CYTHON_CPP(Src Options...)
```

Generates .cpp file from .pyx.

### Macro [BUILDWITH_RAGEL6](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4085) <a name="macro_BUILDWITH_RAGEL6"></a>

```ya.make
BUILDWITH_RAGEL6(Src Options...)
```

Compile .rl file using Ragel6.

### Macro [BUILD_CATBOOST](https://a.yandex-team.ru/arcadia/build/conf/project_specific/other.conf?rev=20020720#L9) <a name="macro_BUILD_CATBOOST"></a>

```ya.make
BUILD_CATBOOST(cbmodel cbname)
```

Generate catboost model and access code.
cbmodel - CatBoost model file name (*.cmb).
cbname - name for a variable (of NCatboostCalcer::TCatboostCalcer type) to be available in CPP code.
CatBoost specific macro.

### Macro [BUILD_ONLY_IF](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_BUILD_ONLY_IF"></a>

```ya.make
BUILD_ONLY_IF([FATAL_ERROR|STRICT] variables)  # builtin
```

Print warning if all variables are false. For example, BUILD_ONLY_IF(LINUX WIN32)
In STRICT mode disables build of all modules and RECURSES of the ya.make.
FATAL_ERROR issues configure error and enables STRICT mode

### Macro [BUILD_YDL_DESC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3753) <a name="macro_BUILD_YDL_DESC"></a>

```ya.make
BUILD_YDL_DESC(Input Symbol Output)
```

Generate a descriptor for a Symbol located in a ydl module Input, and put it to the file Output.

**example:**

    PACKAGE()
        BUILD_YDL_DESC(../types.ydl Event Event.ydld)
    END()

This will parse file ../types.ydl, generate a descriptor for a symbol Event defined in the said file, and put the descriptor to the Event.ydld.

### Macro [BUNDLE](https://a.yandex-team.ru/arcadia/build/plugins/bundle.py?rev=20020720#L6) <a name="macro_BUNDLE"></a>

```ya.make
BUNDLE(<Dir [SUFFIX Suffix] [NAME Name]>...)
```

Brings build artefact from module Dir under optional Name to the current module (e.g. UNION)
If NAME is not specified, the name of the Dir's build artefact will be preserved
Optional SUFFIX allows to use secondary module output. The suffix is appended to the primary output name, so the applicability is limited.
It makes little sense to specify BUNDLE on non-final targets and so this may stop working without prior notice.
Bundle on multimodule will select final target among multimodule variants and will fail if there are none or more than one.

### Macro [BUNDLE_OUTPUT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2903) <a name="macro_BUNDLE_OUTPUT"></a>

```ya.make
BUNDLE_OUTPUT(Dir OutputName [RENAME <Rename>])
```

`Dir` - Path to module
`OutputName` - the name of module artefact to be collected
`Rename` - the output name of collected artefact if needed

### Macro [CFLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4281) <a name="macro_CFLAGS"></a>

```ya.make
CFLAGS([GLOBAL compiler_flag]* compiler_flags)
```

Add the specified flags to the compilation command of C and C++ files.
**params:** GLOBAL - Propagates these flags to dependent projects
Bear in mind that certain flags might be incompatible with certain compilers.

### Macro [CGO_CFLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L461) <a name="macro_CGO_CFLAGS"></a>

```ya.make
CGO_CFLAGS(Flags...)
```

Compiler flags specific to CGO compilation

### Macro [CGO_LDFLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L470) <a name="macro_CGO_LDFLAGS"></a>

```ya.make
CGO_LDFLAGS(Files...)
```

Linker flags specific to CGO linking

### Macro [CGO_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L444) <a name="macro_CGO_SRCS"></a>

```ya.make
CGO_SRCS(Files...)
```

.go sources to be built with CGO

### Macro [CHECK_ALLOWED_PATH](https://a.yandex-team.ru/arcadia/build/internal/plugins/container_layers.py?rev=20020720#L5) <a name="macro_CHECK_ALLOWED_PATH"></a>

_Not documented yet._

### Macro [CHECK_CONTRIB_CREDITS](https://a.yandex-team.ru/arcadia/build/plugins/credits.py?rev=20020720#L11) <a name="macro_CHECK_CONTRIB_CREDITS"></a>

_Not documented yet._

### Macro [CHECK_DEPENDENT_DIRS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L487) <a name="macro_CHECK_DEPENDENT_DIRS"></a>

```ya.make
CHECK_DEPENDENT_DIRS(DENY|ALLOW_ONLY ([ALL|PEERDIRS|GLOB] dir)...)
```

Specify project transitive dependencies constraints.

**params:**
 1. DENY: current module can not depend on module from any specified directory neither directly nor transitively.
 2. ALLOW_ONLY: current module can not depend on module from a dir not specified in the directory list neither directly nor transitively.
 3. ALL: directory constraints following after this modifier are applied to both transitive PEERDIR dependencies and tool dependencies.
 4. PEERDIRS: directory constraints following after this modifier are applied to transitive PEERDIR dependencies only.
 5. GLOB: next directory constraint is an ANT glob pattern.
 6. EXCEPT: next constraint is an exception for the rest of other rules.

Directory constraints added before either ALL or PEERDIRS modifier is used are treated as ALL directory constraints.

**Note:** Can be used multiple times on the same module all specified constraints will be checked.
All macro invocation for the same module must use same constraints type (DENY or ALLOW_ONLY)

### Macro [CHECK_JAVA_DEPS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1838) <a name="macro_CHECK_JAVA_DEPS"></a>

```ya.make
CHECK_JAVA_DEPS(<yes|no|strict>)
```

Check for different classes with duplicate name in classpath.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/

### Macro [CLANG_EMIT_AST_CXX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4972) <a name="macro_CLANG_EMIT_AST_CXX"></a>

```ya.make
CLANG_EMIT_AST_CXX(Input Output Opts...)
```

Emit Clang AST from .cpp file. CXXFLAGS and LLVM_OPTS are passed in, while CFLAGS and C_FLAGS_PLATFORM are not.
**Note:** Output name is used as is, no extension added.

### Macro [CLANG_EMIT_AST_CXX_RUN_TOOL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5785) <a name="macro_CLANG_EMIT_AST_CXX_RUN_TOOL"></a>

```ya.make
CLANG_EMIT_AST_CXX_RUN_TOOL(Tool Args... [SOURCES ...] [OPTS ...] [IN ...] [IN_NOPARSE ...] [TOOL ...] [OUTPUT_INCLUDES ...] [INDUCED_DEPS ...] [IN_DEPS ...] [STDOUT out-file-name] [STDOUT_NOAUTO out-file-name] [CWD cwd])
```

Emit Clang ASTs from .cpp files listed in SOURCES parameter (CXXFLAGS and LLVM_OPTS are passed in, while CFLAGS and C_FLAGS_PLATFORM are not) and run tool Tool with Args... .
OPTS[] parameter is used to pass additional flags to clang. Parameters other than OPTS[] and SOURCES[] are used for runnig a generator (Tool):
- Tool - path to the directory of the tool
- Args... - Tool's arguments
- IN[] - input files required for running the Tool
- IN_NOPARSE[] - input files required for running the Tool, but these files are not parsed for dependencies
- TOOL[] - list of directories of axiliary tools used by Tool
- OUTPUT_INCLUDES[] - includes of the output files which are needed to "build" them
- STDOUT - redirect stdout of the Tool to the output file
- STDOUT_NOAUTO - redirect stdout of the Tool to the output file, but do not chain this file automatically to the processing queue
- CWD - path to the working directory of the Tool
**Note:** Generated AST files generated into BINDIR according to corresponding .cpp file names listed in SOURCES parameter.

### Macro [CLANG_WARNINGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5754) <a name="macro_CLANG_WARNINGS"></a>

```ya.make
CLANG_WARNINGS(Args...)
```

_Not documented yet._

### Macro [CLEAN_TEXTREL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2197) <a name="macro_CLEAN_TEXTREL"></a>

```ya.make
CLEAN_TEXTREL()
```

_Not documented yet._

### Macro [CMAKE_EXPORTED_TARGET_NAME](https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L108) <a name="macro_CMAKE_EXPORTED_TARGET_NAME"></a>

```ya.make
CMAKE_EXPORTED_TARGET_NAME(Name)
```

Forces to use the name given as cmake target name without changing the name of output artefact.
This macro should be used to resolve target name conflicts in  exported cmake project when
changing module name is not applicable. For example both CUDA and non-CUDA py modules for
catboost should have same name lib_catboost.so and both of them are defined as PY_ANY_MODULE(_catboost).
adding CMAKE_EXPORTED_TARGET_NAME(_catboost_non_cuda) to the non CUDA module ya.make file
changes exported cmake target name but preserve generated artefact file name.

### Macro [COLLECT_CONFIG_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5595) <a name="macro_COLLECT_CONFIG_FILES"></a>

```ya.make
COLLECT_CONFIG_FILES(Varname, Dir, Exts...)
```

Recursively collect config files with extensions Exts from Dir and save the result into Varname variable

### Macro [COLLECT_FRONTEND_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5581) <a name="macro_COLLECT_FRONTEND_FILES"></a>

```ya.make
COLLECT_FRONTEND_FILES(Varname, Dir)
```

Recursively collect files with typical frontend extensions from Dir and save the result into variable Varname

### Macro [COLLECT_GO_SWAGGER_FILES](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L5) <a name="macro_COLLECT_GO_SWAGGER_FILES"></a>

```ya.make
COLLECT_GO_SWAGGER_FILES(Varname, Dir)
```

Recursively collect files for swagger config creation

### Macro [COLLECT_JINJA_TEMPLATES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5474) <a name="macro_COLLECT_JINJA_TEMPLATES"></a>

```ya.make
COLLECT_JINJA_TEMPLATES(varname path)
```

This macro collects all jinja and yaml files in the directory specified by second argument and
stores result in the variable with mane specified by first parameter.

### Macro [COLLECT_KOTLIN_LINT_SRCS](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L43) <a name="macro_COLLECT_KOTLIN_LINT_SRCS"></a>

```ya.make
COLLECT_KOTLIN_LINT_SRCS([DIRS dirs] [DIRS_RECURSE dirs_recurse])
```

_Not documented yet._

### Macro [COLLECT_YAML_CONFIG_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5588) <a name="macro_COLLECT_YAML_CONFIG_FILES"></a>

```ya.make
COLLECT_YAML_CONFIG_FILES(Varname, Dir)
```

Recursively collect YAML files except for system-reserved a.yaml ones from Dir and save the result into Varname variable

### Macro [COMPILE_C_AS_CXX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4930) <a name="macro_COMPILE_C_AS_CXX"></a>

```ya.make
COMPILE_C_AS_CXX()
```

Compile .c files as .cpp ones within a module.

### Macro [COMPILE_LUA](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3676) <a name="macro_COMPILE_LUA"></a>

```ya.make
COMPILE_LUA(Src, [NAME <import_name>])
```

Compile LUA source file to object code using LUA 2.0
Optionally override import name which is by default reflects Src name

### Macro [COMPILE_LUA_21](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3695) <a name="macro_COMPILE_LUA_21"></a>

```ya.make
COMPILE_LUA_21(Src, [NAME <import_name>])
```

Compile LUA source file to object code using LUA 2.1
Optionally override import name which is by default reflects Src name

### Macro [COMPILE_LUA_OPENRESTY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3713) <a name="macro_COMPILE_LUA_OPENRESTY"></a>

```ya.make
COMPILE_LUA_OPENRESTY(Src, [NAME <import_name>])
```

Compile LUA source file to object code using OpenResty LUA 2.1
Optionally override import name which is by default reflects Src name

### Macro [CONFIGURE_FILE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4260) <a name="macro_CONFIGURE_FILE"></a>

```ya.make
CONFIGURE_FILE(from to)
```

Copy file with the replacement of configuration variables in form of @ANY_CONF_VAR@ with their values.
The values are collected during configure stage, while replacement itself happens during build stage.
Used implicitly for .in-files processing.

### Macro [CONFTEST_LOAD_POLICY_LEGACY_GLOBAL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1718) <a name="macro_CONFTEST_LOAD_POLICY_LEGACY_GLOBAL"></a>

```ya.make
CONFTEST_LOAD_POLICY_LEGACY_GLOBAL()
```

Explicitly enables legacy `conftest.py` loading mechanism. This is the current default mode.

All conftests from all `PY*_LIBRARY` and `PY*TEST` are pulled in and applied to all tests in non-guatanteed order.

See also: DEVTOOLS-5496

### Macro [CONFTEST_LOAD_POLICY_LOCAL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1706) <a name="macro_CONFTEST_LOAD_POLICY_LOCAL"></a>

```ya.make
CONFTEST_LOAD_POLICY_LOCAL()
```

Loads `conftest.py` files in a way that pytest does it.

pytest documentation about conftest files:
* https://docs.pytest.org/en/stable/reference/fixtures.html#conftest-py-sharing-fixtures-across-multiple-files
* https://docs.pytest.org/en/stable/how-to/writing_plugins.html#plugin-discovery-order-at-tool-startup

**TLDR:**
* For a given test file, fixtures are looked up in `conftest.py` in current and parent directories
* `conftest.py` files in test suite root and parent directories are fully-fledged plugins that can use all hooks
* `conftest.py` files in subdirectories only provide fixtures that are applied to some of the tests

See also: DEVTOOLS-5496

### Macro [CONLYFLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4304) <a name="macro_CONLYFLAGS"></a>

```ya.make
CONLYFLAGS([GLOBAL compiler_flag]* compiler_flags)
```

Add the specified flags to the compilation command of .c (but not .cpp) files.
**params:** GLOBAL - Distributes these flags on dependent projects

### Macro [COPY](https://a.yandex-team.ru/arcadia/build/plugins/cp.py?rev=20020720#L6) <a name="macro_COPY"></a>

_Not documented yet._

### Macro [COPY_FILE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2819) <a name="macro_COPY_FILE"></a>

```ya.make
COPY_FILE(File Destination [AUTO] [OUTPUT_INCLUDES...] [INDUCED_DEPS...] [TEXT])
```

Copy file to build root. It is possible to change both location and the name.

**Parameters:**
- File - Source file name.
- Dest - Output file name.
- AUTO - Consider copied file for further processing automatically.
- OUTPUT_INCLUDES output_includes... - Output file dependencies.
- INDUCED_DEPS $VARs... - Dependencies for generated files. Unlike `OUTPUT_INCLUDES` these may target files further in processing chain.
                          In order to do so VAR should be filled by PREPARE_INDUCED_DEPS macro, stating target files (by type)
                          and set of dependencies
- TEXT - deprecated

The file will be just copied if AUTO boolean parameter is not specified. You should explicitly
mention it in SRCS under new name (or specify AUTO boolean parameter) for further processing.

### Macro [COPY_FILE_WITH_CONTEXT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2839) <a name="macro_COPY_FILE_WITH_CONTEXT"></a>

```ya.make
COPY_FILE_WITH_CONTEXT(File Dest [AUTO] [OUTPUT_INCLUDES...] [INDUCED_DEPS...])
```

Copy file to build root the same way as it is done for COPY_FILE, but also
propagates the context of the source file.

### Macro [COW](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L896) <a name="macro_COW"></a>

```ya.make
COW()
```

_Not documented yet._

### Macro [CPP_ADDINCL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5332) <a name="macro_CPP_ADDINCL"></a>

```ya.make
CPP_ADDINCL(Dirs...)
```

_Not documented yet._

### Macro [CPP_ENUMS_SERIALIZATION](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L824) <a name="macro_CPP_ENUMS_SERIALIZATION"></a>

_Not documented yet._

### Macro [CPP_EVLOG](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L605) <a name="macro_CPP_EVLOG"></a>

```ya.make
CPP_EVLOG()
```

Apply event2cpp proto plugin for all .proto files of PROTO_LIBRARY.
This macro affects only c++ code generation - CPP_PROTO submodule.

### Macro [CPP_EV_PROTO_PLUGIN](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L283) <a name="macro_CPP_EV_PROTO_PLUGIN"></a>

```ya.make
CPP_EV_PROTO_PLUGIN(Name Tool Suf [DEPS <Dependencies>] [EXTRA_OUT_FLAG <ExtraOutFlag>])
```

Like CPP_PROTO_PLUGIN, but also applies the plugin to .ev files in PROTO_LIBRARY.

### Macro [CPP_PROTOLIBS_DEBUG_INFO](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L74) <a name="macro_CPP_PROTOLIBS_DEBUG_INFO"></a>

```ya.make
CPP_PROTOLIBS_DEBUG_INFO()
```

Eqvivalent to NO_DEBUG_INFO() macro if the flag CPP_PROTO_NO_DBGINFO=yes

### Macro [CPP_PROTO_PLUGIN](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L273) <a name="macro_CPP_PROTO_PLUGIN"></a>

```ya.make
CPP_PROTO_PLUGIN(Name Tool Suf [DEPS <Dependencies>] [EXTRA_OUT_FLAG <ExtraOutFlag>])
```

Define protoc plugin for C++ with given Name that emits code into 1 extra output
using Tool. Extra dependencies are passed via DEPS.

### Macro [CPP_PROTO_PLUGIN0](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L259) <a name="macro_CPP_PROTO_PLUGIN0"></a>

```ya.make
CPP_PROTO_PLUGIN0(Name Tool [DEPS <Dependencies>] [EXTRA_OUT_FLAG <ExtraOutFlag>])
```

Define protoc plugin for C++ with given Name that emits code into regular outputs
using Tool. Extra dependencies are passed via DEPS.

### Macro [CPP_PROTO_PLUGIN2](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L294) <a name="macro_CPP_PROTO_PLUGIN2"></a>

```ya.make
CPP_PROTO_PLUGIN2(Name Tool Suf1 Suf2 [DEPS <Dependencies>] [EXTRA_OUT_FLAG <ExtraOutFlag>])
```

Define protoc plugin for C++ with given Name that emits code into 2 extra outputs
using Tool. Extra dependencies are passed via DEPS.

### Macro [CREATE_BUILDINFO_FOR](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4230) <a name="macro_CREATE_BUILDINFO_FOR"></a>

```ya.make
CREATE_BUILDINFO_FOR(GenHdr)
```

Creates header file to access some information about build specified via configuration variables.
Unlike CREATE_SVNVERSION_FOR() it doesn't take revion information from VCS, it uses revision and SandboxTaskId passed via -D options to ya make

### Macro [CREATE_INIT_PY_STRUCTURE](https://a.yandex-team.ru/arcadia/build/plugins/create_init_py.py?rev=20020720#L6) <a name="macro_CREATE_INIT_PY_STRUCTURE"></a>

_Not documented yet._

### Macro [CREDITS_DISCLAIMER](https://a.yandex-team.ru/arcadia/build/plugins/credits.py?rev=20020720#L5) <a name="macro_CREDITS_DISCLAIMER"></a>

_Not documented yet._

### Macro [CTEMPLATE_VARNAMES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4946) <a name="macro_CTEMPLATE_VARNAMES"></a>

```ya.make
CTEMPLATE_VARNAMES(File)
```

Generate File.varnames.h using contrib/libs/ctemplate/make_tpl_varnames_h

**Documentation:** https://github.com/yandex/yatool/tree/main/contrib/libs/ctemplate/README.md

### Macro [CUDA_NVCC_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L39) <a name="macro_CUDA_NVCC_FLAGS"></a>

```ya.make
CUDA_NVCC_FLAGS(compiler flags)
```

Add the specified flags to the compile line .cu-files.

### Macro [CUDA_SRCS](https://a.yandex-team.ru/arcadia/build/plugins/cuda.py?rev=20020720#L15) <a name="macro_CUDA_SRCS"></a>

```ya.make
CUDA_SRCS(File...)
```

A macro for efficient distributed compilation of CUDA code for multiple device architectures.

For each source .cu file multiple nodes are generated:
- node per each device architecture producing PTX and CUBIN
- node merging all PTX and CUBIN files into a single FATBIN blob
- node producing .cpp with host code
- node compiling host .cpp with embedded FATBIN blob

CUDA_ARCHITECTURES variable is used to determine the list of architectures to compile device code for.

### Macro [CUSTOM_LINK_STEP_SCRIPT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1356) <a name="macro_CUSTOM_LINK_STEP_SCRIPT"></a>

```ya.make
CUSTOM_LINK_STEP_SCRIPT(name)
```

Specifies name of a script for custom link step. The scripts
should be placed in the build/scripts directory and are subject to
review by devtools@.

### Macro [CXXFLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4311) <a name="macro_CXXFLAGS"></a>

```ya.make
CXXFLAGS(compiler_flags)
```

Add the specified flags to the compilation command of .cpp (but not .c) files.

### Macro [CYTHON_FLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4318) <a name="macro_CYTHON_FLAGS"></a>

```ya.make
CYTHON_FLAGS(compiler_flags)
```

Add the specified flags to the compilation command of .pyx files.

### Macro [DARWIN_SIGNED_RESOURCE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5524) <a name="macro_DARWIN_SIGNED_RESOURCE"></a>

```ya.make
DARWIN_SIGNED_RESOURCE(Resource, Relpath)
```

_Not documented yet._

### Macro [DARWIN_STRINGS_RESOURCE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5520) <a name="macro_DARWIN_STRINGS_RESOURCE"></a>

```ya.make
DARWIN_STRINGS_RESOURCE(Resource, Relpath)
```

_Not documented yet._

### Macro [DATA](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1618) <a name="macro_DATA"></a>

```ya.make
DATA([path...])
```

Specifies the path to the data necessary test.
Valid values are: arcadia/<path> , arcadia_tests_data/<path> and sbr://<resource_id>.
In the latter case resource will be brought to the working directory of the test before it is started

Used only inside TEST modules.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#dannyeizrepozitorija

### Macro [DATA_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1644) <a name="macro_DATA_FILES"></a>

```ya.make
DATA_FILES([path...])
```

Specifies the path to the arcadia source data necessary test.
Used only inside TEST modules.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#dannyeizrepozitorija

### Macro [DEB_VERSION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4557) <a name="macro_DEB_VERSION"></a>

```ya.make
DEB_VERSION(File)
```

Creates a header file DebianVersion.h define the DEBIAN_VERSION taken from the File.

### Macro [DECIMAL_MD5_LOWER_32_BITS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4243) <a name="macro_DECIMAL_MD5_LOWER_32_BITS"></a>

```ya.make
DECIMAL_MD5_LOWER_32_BITS(<fileName> [FUNCNAME funcName] [inputs...])
```

Generates .cpp file <fileName> with one defined function 'const char* <funcName>() { return "<calculated_md5_hash>"; }'.
<calculated_md5_hash> will be md5 hash for all inputs passed to this macro.

### Macro [DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE"></a>

```ya.make
DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE(name sbr:id FOR platform1 sbr:id FOR platform2...)  #builtin
```

Associate name with sbr-id on platform.

Ask devtools@yandex-team.ru if you need more information

### Macro [DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON"></a>

```ya.make
DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(VarName, FileName [, FriendlyResourceName])
```

Associate 'Name' with a platform to resource uri mapping
File 'FileName' contains json with a 'canonized platform -> resource uri' mapping.
'FriendlyResourceName', if specified, is used in configuration error messages instead of VarName.
The mapping file format see in SET_RESOURCE_URI_FROM_JSON description.

### Macro [DECLARE_EXTERNAL_HOST_RESOURCES_PACK](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DECLARE_EXTERNAL_HOST_RESOURCES_PACK"></a>

```ya.make
DECLARE_EXTERNAL_HOST_RESOURCES_PACK(RESOURCE_NAME name sbr:id FOR platform1 sbr:id FOR platform2... RESOURCE_NAME name1 sbr:id1 FOR platform1...)  #builtin
```

Associate name with sbr-id on platform.

Ask devtools@yandex-team.ru if you need more information

### Macro [DECLARE_EXTERNAL_RESOURCE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DECLARE_EXTERNAL_RESOURCE"></a>

```ya.make
DECLARE_EXTERNAL_RESOURCE(name sbr:id name1 sbr:id1...)  #builtin
```

Associate name with sbr-id.

Ask devtools@yandex-team.ru if you need more information

### Macro [DECLARE_EXTERNAL_RESOURCE_BY_JSON](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DECLARE_EXTERNAL_RESOURCE_BY_JSON"></a>

```ya.make
DECLARE_EXTERNAL_RESOURCE_BY_JSON(VarName, FileName [, FriendlyResourceName])
```

Associate 'Name' with a resource for the current target platform
File 'FileName' contains json with a 'canonized platform -> resource uri' mapping.
'FriendlyResourceName', if specified, is used in configuration error messages instead of VarName.
The mapping file format see in SET_RESOURCE_URI_FROM_JSON description.

### Macro [DECLARE_IN_DIRS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4751) <a name="macro_DECLARE_IN_DIRS"></a>

```ya.make
DECLARE_IN_DIRS(var_prefix files_mask DIRS dirs [RECURSIVE] [EXCLUDES excludes] [SRCDIR srcdir])
```

This macro allow passing content of directories to macros like `RUN_PROGRAM` and `RUN_PYTHON3` as IN parameter.

The content is matched by following rules:
- The files are looked in <srcdir>. The srcdir is relative to module directory and defaulted to module directory.
- Inside <srcdir> files are looked in all <dirs>, recursively or not depending on RECURSIVE parameter.
- Files are matched by file_mask which may contain * or ?.
- <excludes> are then applied over matched files. Excludes are regular globs including recursive parts support.

Taking `var_prefix` macro declared 4 variables:
- <var_prefix>_FILES - the file list matched by the macro using rules above. This variable can be passed to `IN` parameter of `RUN_PROGRAM` and alikes.
                       Also it may be passed escaped as argument to tool/script. See example below.
- <var_prefix>_PATTERNS - the glob patterns used for match.
- <var_prefix>_EXCLUDES - exclude patterns from EXCLUDES argument and ones to exclude ya.make and a.yaml
- <var_prefix>_SRCDIR - value of SRCDIR argument

**Parameters:**
- var_prefix - Mandatory prefix of variables the macro declares
- file_mask - Mandatory glob-like mask for files
  file_mask should not conatain '**'
- DIRS dirs - Mandatory list of dirs relative to srcdir or current one in which files should be looked
  Dirs cannot contain ${ARCADIA_ROOT} (and other similar vars), '..', '*' or '?'.
- RECURSIVE - Optional request to lookup dirs recursively. Default is non-recursive lookup
- EXCLUDES excludes - Optional list of globs to exclude from match.
- SRCDIR srcdir - Optional directory (relative to current one) to apply globs. Default is the current dir.
  We strongly discourage this, but srcdir may contain '..' or start from ${ARCADIA_ROOT} for root-relative addressing.
  scrdir cannot contain any of '*' or '?'

**Examples:**
```
DECLARE_IN_DIRS(TXT *.txt DIRS . EXCLUDE .*.txt **/.*.txt)
\# file list requires escaping as argument
RUN_PYTHON3(concat.py \${TXT_FILES} IN ${TXT_FILES} STDOUT concatenated.txt)
```

```
DECLARE_IN_DIRS(ALL_TXT *.txt SRCDIR txt RECURSIVE DIRS design rules EXCLUDE all.txt)
RUN_PYTHON3(concat_all.py --dirs ${MODDIR}/${ALL_TXT_SRCDIR} --pattern ${ALL_TXT_PATTERNS} --exclude ${ALL_TXT_EXCLUDES} IN ${ALL_TXT_FILES} STDOUT concatenated.txt)
```

```
DECLARE_IN_DIRS(D_TXT *.txt SRCDIR txt/design EXCLUDE all.txt DIRS .)
DECLARE_IN_DIRS(R_TXT *.txt SRCDIR txt/rules DIRS .)
RUN_PYTHON3(concat_all.py --dirs ${MODDIR}/${D_TXT_SRCDIR} ${MODDIR}/${R_TXT_SRCDIR} --patterns ${D_TXT_PATTERNS} ${R_TXT_PATTERNS} --exclude ${D_TXT_EXCLUDES} IN ${D_TXT_FILES} ${R_TXT_FILES} STDOUT concatenated.txt)
```

**Notes:**
1. All 'ya.make' and 'a.yaml' files are excluded from match.
2. Matched files are never parsed for dependencies even though they shall be passed to IN, not to IN_NOPARSE.
3. The list of files expanded late and will not work in macros like SRCS. This macro only meant for use with generating macros like RUN_PROGRAM processing entire matching list with one command.
4. We support extended file mask syntax for multiple masks like "(*.cpp|*.h)". However, this will be preserved in <var_prefix>_PATTERNS variable and so tool/script either should support such syntax or
   or should not rely on value of the variable for actual matching.
5. There is known issue with empty match and escaped substitution like `concat.py \${TXT_FILES}`. It may result in weird errors and can be workarounded by extra argument like `concat.py - \${TXT_FILES}`
6. EXCLUDES work differently with SRCDIR is specified. Use discriminating tail of SRCDIR in order to match exact files non-recursively.
   E.g. if SRCDIR is a/b/zz and EXCLUDE is *.x the exclude will work recursively on all matches including zz's child dierctories. To limit match to zz's level use EXCLUDE zz/*.x instead.
6. Parameters of macro are somewhat validated and we may add extra checks in the fulture including protection over too broad match.

### Macro [DEFAULT](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DEFAULT"></a>

```ya.make
DEFAULT(varname value)  #builtin
```

Sets varname to value if value is not set yet

### Macro [DEFAULT_JAVA_SRCS_LAYOUT](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L565) <a name="macro_DEFAULT_JAVA_SRCS_LAYOUT"></a>

```ya.make
DEFAULT_JAVA_SRCS_LAYOUT()
```

Configures standard Maven/Gradle directory layout for main sources:
  - Java/Kotlin sources: src/main/java/**/*.java (and Kotlin equivalents)
  - Resources: src/main/resources/**/*

### Macro [DEFAULT_JDK_VERSION](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2469) <a name="macro_DEFAULT_JDK_VERSION"></a>

```ya.make
DEFAULT_JDK_VERSION(Version)
```

Specify JDK version for module, can be overridden by setting the JDK_VERSION variable

### Macro [DEFAULT_JUNIT_JAVA_SRCS_LAYOUT](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L583) <a name="macro_DEFAULT_JUNIT_JAVA_SRCS_LAYOUT"></a>

```ya.make
DEFAULT_JUNIT_JAVA_SRCS_LAYOUT()
```

Configures standard Maven/Gradle directory layout for JUnit tests:
  - Java/Kotlin sources: java/**/*.java (and Kotlin equivalents)
  - Test resources: resources/**/*

**Note:** This macro assumes it's called from within src/test directory context.
The actual paths will be:
  - Sources: src/test/java/**/*.java
  - Resources: src/test/resources/**/*

### Macro [DEPENDENCY_MANAGEMENT](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2178) <a name="macro_DEPENDENCY_MANAGEMENT"></a>

```ya.make
DEPENDENCY_MANAGEMENT(path/to/lib1 path/to/lib2 ...)
```

Lock version of the library from the contrib/java at some point, so that all unversioned PEERDIRs to this library refer to the specified version.

For example, if the module has PEERDIR (contrib/java/junit/junit), and
  1. specifies DEPENDENCY_MANAGEMENT(contrib/java/junit/junit/4.12),
     the PEERDIR is automatically replaced by contrib/java/junit/junit/4.12;
  2. doesn't specify DEPENDENCY_MANAGEMENT, PEERDIR automatically replaced
     with the default from contrib/java/junit/junit/ya.make.
     These defaults are always there and are supported by maven-import, which puts
     there the maximum version available in contrib/java.

The property is transitive. That is, if module A PEERDIRs module B, and B has PEERDIR(contrib/java/junit/junit), and this junit was replaced by junit-4.12, then junit-4.12 will come to A through B.

If some module has both DEPENDENCY_MANAGEMENT(contrib/java/junit/junit/4.12) and PERDIR(contrib/java/junit/junit/4.11), the PEERDIR wins.

**Documentation:** https://wiki.yandex-team.ru/yatool/java/

### Macro [DEPENDS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DEPENDS"></a>

```ya.make
DEPENDS(path1 [path2...]) # builtin
```

Buildable targets that should be brought to the test run. This dependency isonly used when tests run is requested. It will build the specified modules andbring them to the working directory of the test (in their Arcadia paths). Itis reasonable to specify only final targets her (like programs, DLLs orpackages). DEPENDS to UNION is the only exception: UNIONs aretransitively closed at DEPENDS bringing all dependencies to the test.

DEPENDS on multimodule will select and bring single final target. If more noneor more than one final target available in multimodule DEPENDS to it willproduce configuration error.

### Macro [DIRECT_DEPS_ONLY](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2347) <a name="macro_DIRECT_DEPS_ONLY"></a>

Add direct PEERDIR's only in java compile classpath

### Macro [DISABLE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DISABLE"></a>

```ya.make
DISABLE(varname)  #builtin
```

Sets varname to 'no'

### Macro [DISABLE_DATA_VALIDATION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1602) <a name="macro_DISABLE_DATA_VALIDATION"></a>

```ya.make
DISABLE_DATA_VALIDATION(Args...)
```

_Not documented yet._

### Macro [DLL_FOR](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_DLL_FOR"></a>

```ya.make
DLL_FOR(path/to/lib [libname] [major_ver [minor_ver]] [EXPORTS symlist_file])  #builtin
```

DLL module definition based on specified LIBRARY

### Macro [DOCKER_IMAGE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1634) <a name="macro_DOCKER_IMAGE"></a>

```ya.make
DOCKER_IMAGE(Images...)
```

_Not documented yet._

### Macro [DOCS_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L327) <a name="macro_DOCS_CONFIG"></a>

```ya.make
DOCS_CONFIG(path)
```

Specify path to config file for DOCS multimodule if it differs from default path.
If used for [MKDOCS](#multimodule_MKDOCS) multimodule the default path is "%%project_directory%%/mkdocs.yml".
If used for [DOCS](#multimodule_DOCS) multimodule the default path is "%%project_directory%%/.yfm".
Path must be either Arcadia root relative.

**See also:** [DOCS](#multimodule_DOCS)

### Macro [DOCS_COPY_FILES](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L13) <a name="macro_DOCS_COPY_FILES"></a>

```ya.make
DOCS_COPY_FILES(FROM src_dir [NAMESPCE dst_dir] files...)
```

Copy files from src_dir to $BINDIR/dst_dir

### Macro [DOCS_DIR](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L288) <a name="macro_DOCS_DIR"></a>

```ya.make
DOCS_DIR(path)
```

Specify directory with source .md files for DOCS multimodule if it differs from project directory.
Path must be Arcadia root relative.

**See also:** [DOCS](#multimodule_DOCS)

### Macro [DOCS_HTML_FROM](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L254) <a name="macro_DOCS_HTML_FROM"></a>

```ya.make
DOCS_HTML_FROM(Dir Name)
```

`Dir` - the path to DOCS module
`Name` - the name of html archive for the DOCS module in `Dir`

### Macro [DOCS_INCLUDE_SOURCES](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L351) <a name="macro_DOCS_INCLUDE_SOURCES"></a>

```ya.make
DOCS_INCLUDE_SOURCES(path...)
```

Specify a list of paths to source code files which will be used as text includes in a documentation project.
Paths must be Arcadia root relative.

**See also:** [DOCS](#multimodule_DOCS)

### Macro [DOCS_VARS](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L339) <a name="macro_DOCS_VARS"></a>

```ya.make
DOCS_VARS(variable1=value1 variable2=value2 ...)
```

Specify a set of default values of template variables for DOCS multimodule.
There must be no spaces around "=". Values will be treated as strings.

**See also:** [DOCS](#multimodule_DOCS)

### Macro [DYNAMIC_LIBRARY_FROM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2294) <a name="macro_DYNAMIC_LIBRARY_FROM"></a>

```ya.make
DYNAMIC_LIBRARY_FROM(Paths)
```

Use specified libraries as sources of DLL

### Macro [ELSE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_ELSE"></a>

```ya.make
IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin
```

Apply macros if none of previous conditions hold

### Macro [ELSEIF](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_ELSEIF"></a>

```ya.make
IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin
```

Apply macros if other_condition holds while none of previous conditions hold

### Macro [EMBED_JAVA_VCS_INFO](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L453) <a name="macro_EMBED_JAVA_VCS_INFO"></a>

```ya.make
EMBED_JAVA_VCS_INFO()
```

Embed manifest with vcs info into `EXTERNAL_JAVA_LIBRARY`
By default this is disabled.

### Macro [ENABLE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_ENABLE"></a>

```ya.make
ENABLE(varname)  #builtin
```

Sets varname to 'yes'

### Macro [ENABLE_PREVIEW](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2047) <a name="macro_ENABLE_PREVIEW"></a>

```ya.make
ENABLE_PREVIEW()
```

Enable java preview features.

### Macro [END](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_END"></a>

```ya.make
END()  # builtin
```

The end of the module

### Macro [ENDIF](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_ENDIF"></a>

```ya.make
IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin
```

End of conditional construct

### Macro [ENV](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1687) <a name="macro_ENV"></a>

```ya.make
ENV(key[=value])
```

Sets env variable key to value (gets value from system env by default).

### Macro [EVLOG_CMD](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1072) <a name="macro_EVLOG_CMD"></a>

```ya.make
EVLOG_CMD(SRC)
```

_Not documented yet._

### Macro [EXCLUDE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2091) <a name="macro_EXCLUDE"></a>

```ya.make
EXCLUDE(prefixes)
```

Specifies which libraries should be excluded from the classpath.

### Macro [EXCLUDE_TAGS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_EXCLUDE_TAGS"></a>

```ya.make
EXCLUDE_TAGS(tags...)  # builtin
```

Instantiate from multimodule all variants except ones with tags listed

### Macro [EXPERIMENTAL_FORK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3008) <a name="macro_EXPERIMENTAL_FORK"></a>

```ya.make
EXPERIMENTAL_FORK()
```

Only for java-tests: same as FORK_(SUB)TESTS(SEQUENTIAL) for other languages
Compatible with FORK_(SUB)TESTS.

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [EXPLICIT_DATA](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1654) <a name="macro_EXPLICIT_DATA"></a>

```ya.make
EXPLICIT_DATA()
```

_Not documented yet._

### Macro [EXPLICIT_OUTPUTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5151) <a name="macro_EXPLICIT_OUTPUTS"></a>

```ya.make
EXPLICIT_OUTPUTS(Files...)
```

Let UNION has only explicitly specified outputs listed in this macro
The list of files shall contain results of commands in this UNION.
Only these files will be outputs of the UNION. This allows to eliminate
intermediate files being result of the UNION

### Macro [EXPORTS_SCRIPT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1320) <a name="macro_EXPORTS_SCRIPT"></a>

```ya.make
EXPORTS_SCRIPT(exports_file)
```

Specify exports script within PROGRAM, DLL and DLL-derived modules.
This accepts 2 kind of files: .exports with <lang symbol> pairs and JSON-line .symlist files.
The other option use EXPORTS parameter of the DLL module itself.

**See also:** [DLL](#module_DLL)

### Macro [EXPORT_ALL_DYNAMIC_SYMBOLS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1338) <a name="macro_EXPORT_ALL_DYNAMIC_SYMBOLS"></a>

```ya.make
EXPORT_ALL_DYNAMIC_SYMBOLS()
```

Export all non-hidden symbols as dynamic when linking a PROGRAM.

### Macro [EXTERNAL_RESOURCE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_EXTERNAL_RESOURCE"></a>

```ya.make
EXTERNAL_RESOURCE(...)  #builtin, deprecated
```

Don't use this. Use RESOURCE_LIBRARY or FROM_SANDBOX instead

### Macro [EXTRADIR](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_EXTRADIR"></a>

```ya.make
EXTRADIR(...)  #builtin, deprecated
```

Ignored

### Macro [EXTRALIBS_STATIC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2767) <a name="macro_EXTRALIBS_STATIC"></a>

```ya.make
EXTRALIBS_STATIC(Libs...)
```

Add the specified external static libraries to the program link

### Macro [FBS_CMD](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L153) <a name="macro_FBS_CMD"></a>

```ya.make
FBS_CMD(SRC, SRCFLAGS...)
```

_Not documented yet._

### Macro [FBS_NAMESPACE](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L95) <a name="macro_FBS_NAMESPACE"></a>

```ya.make
FBS_NAMESPACE(NAMESPACE, PATH...)
```

_Not documented yet._

### Macro [FBS_TO_PY2SRC](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L28) <a name="macro_FBS_TO_PY2SRC"></a>

```ya.make
FBS_TO_PY2SRC(OUT_NAME, IN_FBS_FILES...)
```

_Not documented yet._

### Macro [FILES](https://a.yandex-team.ru/arcadia/build/plugins/files.py?rev=20020720#L4) <a name="macro_FILES"></a>

_Not documented yet._

### Macro [FLATC_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L10) <a name="macro_FLATC_FLAGS"></a>

```ya.make
FLATC_FLAGS(flags...)
```

Add flags to flatc command line

### Macro [FLAT_JOIN_SRCS_GLOBAL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3050) <a name="macro_FLAT_JOIN_SRCS_GLOBAL"></a>

```ya.make
FLAT_JOIN_SRCS_GLOBAL(Out Src...)
```

Join set of sources into single file named Out and send it for further processing as if it were listed as SRCS(GLOBAL Out).
This macro places all files into single file, so will work with any sources.
You should specify file name with the extension as Out. Further processing will be done according to this extension.

### Macro [FLEX_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L50) <a name="macro_FLEX_FLAGS"></a>

```ya.make
FLEX_FLAGS(<flags>)
```

Set flags for Lex tool (flex) invocations.

### Macro [FLEX_GEN_C](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L80) <a name="macro_FLEX_GEN_C"></a>

```ya.make
FLEX_GEN_C()
```

Generate C from Lex grammar. The C++ is generated by default.

### Macro [FLEX_GEN_CPP](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L87) <a name="macro_FLEX_GEN_CPP"></a>

```ya.make
FLEX_GEN_CPP()
```

Generate C++ from Lex grammar. This is current default.

### Macro [FORK_SUBTESTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2970) <a name="macro_FORK_SUBTESTS"></a>

```ya.make
FORK_SUBTESTS()
```

Splits the test run in chunks on subtests.
The number of chunks can be overridden using the macro SPLIT_FACTOR.

Allows to run tests in parallel. Supported in UNITTEST, JTEST/JUNIT5 and PY2TEST/PY3TEST modules.

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [FORK_TESTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2956) <a name="macro_FORK_TESTS"></a>

```ya.make
FORK_TESTS()
```

Splits a test run on chunks by test classes.
The number of chunks can be overridden using the macro SPLIT_FACTOR.

Allows to run tests in parallel. Supported in UNITTEST, JTEST/JUNIT5 and PY2TEST/PY3TEST modules.

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [FORK_TEST_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2996) <a name="macro_FORK_TEST_FILES"></a>

```ya.make
FORK_TEST_FILES()
```

Only for PY2TEST and PY3TEST: splits a file executable with the tests on chunks in the files listed in TEST_SRCS
Compatible with FORK_(SUB)TESTS.

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [FROM_ARCHIVE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4915) <a name="macro_FROM_ARCHIVE"></a>

```ya.make
FROM_ARCHIVE(Src [RENAME <resource files>] OUT_[NOAUTO] <output files> [EXECUTABLE] [OUTPUT_INCLUDES <include files>] [INDUCED_DEPS $VARs...])
```

Process file archive as [FROM_SANDBOX()](#macro_FROM_SANDBOX).

### Macro [FROM_SANDBOX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4895) <a name="macro_FROM_SANDBOX"></a>

```ya.make
FROM_SANDBOX([FILE] resource_id [AUTOUPDATED script] [RENAME <resource files>] OUT_[NOAUTO] <output files> [EXECUTABLE] [OUTPUT_INCLUDES <include files>] [INDUCED_DEPS $VARs...])
```

Download the resource from the Sandbox, unpack (if not explicitly specified word FILE) and add OUT files to the build. EXECUTABLE makes them executable.
You may specify extra dependencies that output files bring using OUTPUT_INCLUDES or INDUCED_DEPS. The change of these may e.g. lead to recompilation of .cpp files extracted from resource.
If there is no default processing for OUT files or you need process them specially use OUT_NOAUTO instead of OUT.

It is disallowed to specify directory as OUT/OUT_NOAUTO since all outputs of commands shall be known to build system.

RENAME renames files to the corresponding OUT and OUT_NOAUTO outputs:
FROM_SANDBOX(resource_id RENAME in_file1 in_file2 OUT out_file1 out_file2 out_file3)
FROM_SANDBOX(resource_id RENAME in_file1 OUT out_file1 RENAME in_file2 OUT out_file2)
FROM_SANDBOX(FILE resource_id RENAME resource_file OUT out_name)

RENAME RESOURCE allows to rename the resource without specifying its file name.

OUTPUT_INCLUDES output_includes... - Includes of the output files that are needed to build them.
INDUCED_DEPS $VARs... - Dependencies for generated files. Unlike `OUTPUT_INCLUDES` these may target files further in processing chain.
                        In order to do so VAR should be filled by PREPARE_INDUCED_DEPS macro, stating target files (by type) and set of dependencies

If AUTOUPDATED is specified than macro will be regularly updated according to autoupdate script. The dedicated Sandbox task scans the arcadia and
changes resource_ids in such macros if newer resource of specified type is available. Note that the task seeks AUTOUPDATED in specific position,
so you shall place it immediately after resource_id.

### Macro [FULL_JAVA_SRCS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L553) <a name="macro_FULL_JAVA_SRCS"></a>

Fill JAVA_SRCS to value for ya ide idea and real apply for late globs

### Macro [FUNCTION_ORDERING_FILE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L151) <a name="macro_FUNCTION_ORDERING_FILE"></a>

```ya.make
FUNCTION_ORDERING_FILE(VAR_NAME)
```

Select file for function reordering. Works only with lld linker.
VAR_NAME should be the same value that was passed into DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE library.

### Macro [FUZZ_DICTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1554) <a name="macro_FUZZ_DICTS"></a>

```ya.make
FUZZ_DICTS(path1 [path2...])
```

Allows you to specify dictionaries, relative to the root of Arcadia, which will be used in Fuzzing.
Libfuzzer and AFL use a single syntax for dictionary descriptions.
Should only be used in FUZZ modules.

**Documentation:** https://wiki.yandex-team.ru/yatool/fuzzing/

### Macro [FUZZ_OPTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1573) <a name="macro_FUZZ_OPTS"></a>

```ya.make
FUZZ_OPTS(opt1 [Opt2...])
```

Overrides or adds options to the corpus mining and fuzzer run.
Currently supported only Libfuzzer, so you should use the options for it.
Should only be used in FUZZ modules.

**example:**

    FUZZ_OPTS (
        -max_len=1024
        -rss_limit_mb=8192
    )

**Documentation:** https://wiki.yandex-team.ru/yatool/fuzzing/

### Macro [GENERATE_ENUM_SERIALIZATION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4536) <a name="macro_GENERATE_ENUM_SERIALIZATION"></a>

```ya.make
GENERATE_ENUM_SERIALIZATION(File.h)
```

Create serialization support for enumeration members defined in the header (String <-> Enum conversions) and compile it into the module.

**Documentation:** https://wiki.yandex-team.ru/yatool/HowToWriteYaMakeFiles/

### Macro [GENERATE_ENUM_SERIALIZATION_WITH_HEADER](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4548) <a name="macro_GENERATE_ENUM_SERIALIZATION_WITH_HEADER"></a>

```ya.make
GENERATE_ENUM_SERIALIZATION_WITH_HEADER(File.h)
```

Create serialization support for enumeration members defined in the header (String <-> Enum conversions) and compile it into the module
Provide access to serialization functions via generated header File_serialized.h

**Documentation:** https://wiki.yandex-team.ru/yatool/HowToWriteYaMakeFiles/

### Macro [GENERATE_IMPLIB](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5840) <a name="macro_GENERATE_IMPLIB"></a>

```ya.make
GENERATE_IMPLIB(Lib, Path, [SONAME Name])
```

Generates a wrapper for external dynamic library using Implib.so and excludes the real library from linker command

The wrapper loads the real library on the first call to any of its functions

**example:**

    PEERDIR(build/internal/platform/cuda)

    GENERATE_IMPLIB(cuda $CUDA_TARGET_ROOT/lib64/stubs/libcuda.so SONAME libcuda.so.1)

### Macro [GENERATE_PY_PROTOS](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L671) _(deprecated)_ <a name="macro_GENERATE_PY_PROTOS"></a>

```ya.make
GENERATE_PY_PROTOS(ProtoFiles...)
```

Generate python bindings for protobuf files.
Macro is obsolete and not recommended for use!

### Macro [GENERATE_SCRIPT](https://a.yandex-team.ru/arcadia/build/plugins/java.py?rev=20020720#L297) <a name="macro_GENERATE_SCRIPT"></a>

_Not documented yet._

### Macro [GENERATE_YT_RECORD](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yt.conf?rev=20020720#L7) <a name="macro_GENERATE_YT_RECORD"></a>

```ya.make
GENERATE_YT_RECORD(Yaml, OUTPUT_INCLUDES[])
```

_Not documented yet._

### Macro [GEN_SCHEEME2](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4643) <a name="macro_GEN_SCHEEME2"></a>

```ya.make
GEN_SCHEEME2(scheeme_name from_file dependent_files...)
```

Generates a C++ description for structure(contains the field RecordSig) in the specified file (and connected).

1. ${scheeme_name}.inc - the name of the generated file.
2. Use an environment variable - DATAWORK_SCHEEME_EXPORT_FLAGS that allows to specify flags to tools/structparser

**example:**

    SET(DATAWORK_SCHEEME_EXPORT_FLAGS --final_only -m "::")

all options are passed to structparser (in this example --final_only - do not export heirs with public base that contains the required field,,- m "::" only from the root namespace)
sets in extra option

**example:**

    SET(EXTRACT_STRUCT_INFO_FLAGS -f \"const static ui32 RecordSig\"
        -u \"RecordSig\" -n${scheeme_name}SchemeInfo ----gcc44_no_typename no_complex_overloaded_func_export
        ${DATAWORK_SCHEEME_EXPORT_FLAGS})

for compatibility with C++ compiler and the external environment.
See tools/structparser for more details.

### Macro [GLOBAL_CFLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4289) <a name="macro_GLOBAL_CFLAGS"></a>

```ya.make
GLOBAL_CFLAGS(compiler_flags)
```

Add the specified flags to the compilation command of C and C++ files and propagate these flags to dependent projects

### Macro [GLOBAL_SRCS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2436) <a name="macro_GLOBAL_SRCS"></a>

```ya.make
GLOBAL_SRCS(filenames...)
```

Make all source files listed as GLOBAL.
Call to GLOBAL_SRCS macro is equivalent to call to SRCS macro when each source file is marked with GLOBAL keyword.
Arcadia root relative or project dir relative paths are supported for filenames arguments. GLOBAL keyword is not
recognized for GLOBAL_SRCS in contrast to SRCS macro.

**example:**
Consider the file to ya.make:

    LIBRARY()
        GLOBAL_SRCS(foo.cpp bar.cpp)
    END()

**See also:** [SRCS()](#macro_SRCS)

### Macro [GOLANG_VERSION](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L187) <a name="macro_GOLANG_VERSION"></a>

```ya.make
GOLANG_VERSION(Arg)
```

_Not documented yet._

### Macro [GO_ASM_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L149) <a name="macro_GO_ASM_FLAGS"></a>

```ya.make
GO_ASM_FLAGS(flags)
```

Add the specified flags to the go asm compile command line.

### Macro [GO_BENCH_TIMEOUT](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1128) <a name="macro_GO_BENCH_TIMEOUT"></a>

```ya.make
GO_BENCH_TIMEOUT(x)
```

Sets timeout in seconds for 1 Benchmark in go benchmark suite

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [GO_CGO1_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L157) <a name="macro_GO_CGO1_FLAGS"></a>

```ya.make
GO_CGO1_FLAGS(flags)
```

Add the specified flags to the go cgo compile command line.

### Macro [GO_CGO2_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L165) <a name="macro_GO_CGO2_FLAGS"></a>

```ya.make
GO_CGO2_FLAGS(flags)
```

Add the specified flags to the go cgo compile command line.

### Macro [GO_COMPILE_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L173) <a name="macro_GO_COMPILE_FLAGS"></a>

```ya.make
GO_COMPILE_FLAGS(flags)
```

Add the specified flags to the go compile command line.

### Macro [GO_EMBED_BINDIR](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L576) <a name="macro_GO_EMBED_BINDIR"></a>

```ya.make
GO_EMBED_BINDIR(DIR)
```

Define an embed directory DIR for files from ARCADIA_BUILD_ROOT

### Macro [GO_EMBED_DIR](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L543) <a name="macro_GO_EMBED_DIR"></a>

```ya.make
GO_EMBED_DIR(DIR)
```

Define an embed directory DIR.

### Macro [GO_EMBED_PATTERN](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L507) <a name="macro_GO_EMBED_PATTERN"></a>

```ya.make
GO_EMBED_PATTERN(PATTERN)
```

Define an embed pattern.

### Macro [GO_EMBED_TEST_DIR](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L551) <a name="macro_GO_EMBED_TEST_DIR"></a>

```ya.make
GO_EMBED_TEST_DIR(DIR)
```

Define an embed directory DIR for internal go tests.

### Macro [GO_EMBED_XTEST_DIR](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L559) <a name="macro_GO_EMBED_XTEST_DIR"></a>

```ya.make
GO_EMBED_XTEST_DIR(DIR)
```

Define an embed directory DIR for external go tests.

### Macro [GO_GRPC_GATEWAY_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L658) <a name="macro_GO_GRPC_GATEWAY_SRCS"></a>

```ya.make
GO_GRPC_GATEWAY_SRCS()
```

Use of grpc-gateway plugin (Supported for Go only).

### Macro [GO_GRPC_GATEWAY_SWAGGER_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L666) <a name="macro_GO_GRPC_GATEWAY_SWAGGER_SRCS"></a>

```ya.make
GO_GRPC_GATEWAY_SWAGGER_SRCS()
```

Use of grpc-gateway plugin w/ swagger emission (Supported for Go only).

### Macro [GO_GRPC_GATEWAY_V2_OPENAPI_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L718) <a name="macro_GO_GRPC_GATEWAY_V2_OPENAPI_SRCS"></a>

```ya.make
GO_GRPC_GATEWAY_V2_OPENAPI_SRCS(Files...)
```

Use of grpc-gateway plugin w/ openapi v2 emission (Supported for Go only).

### Macro [GO_GRPC_GATEWAY_V2_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L736) <a name="macro_GO_GRPC_GATEWAY_V2_SRCS"></a>

```ya.make
GO_GRPC_GATEWAY_V2_SRCS()
```

Use of grpc-gateway plugin (Supported for Go only).

### Macro [GO_LDFLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L453) <a name="macro_GO_LDFLAGS"></a>

```ya.make
GO_LDFLAGS(Flags...)
```

Link flags for GO_PROGRAM linking from .go sources

### Macro [GO_LINK_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L181) <a name="macro_GO_LINK_FLAGS"></a>

```ya.make
GO_LINK_FLAGS(flags)
```

Add the specified flags to the go link command line.

### Macro [GO_MOCKGEN_CONTRIB_FROM](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1207) <a name="macro_GO_MOCKGEN_CONTRIB_FROM"></a>

```ya.make
GO_MOCKGEN_CONTRIB_FROM(Path)
```

Part of Go mock module definition, both reflect and source mode.
Defines path for mock interfaces source for contrib (vendored) sources

### Macro [GO_MOCKGEN_FROM](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1197) <a name="macro_GO_MOCKGEN_FROM"></a>

```ya.make
GO_MOCKGEN_FROM(Path)
```

Part of Go mock module definition, both reflect and source mode.
Defines path for mock interfaces source

### Macro [GO_MOCKGEN_MOCKS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1258) <a name="macro_GO_MOCKGEN_MOCKS"></a>

```ya.make
GO_MOCKGEN_MOCKS()
```

Part of Go mock module definition, reflect mode.
Generates mocks, expect to have `gen` folder with GO_MOCKGEN_REFLECT

### Macro [GO_MOCKGEN_PACKAGE](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1223) <a name="macro_GO_MOCKGEN_PACKAGE"></a>

```ya.make
GO_MOCKGEN_PACKAGE(package)
```

Part of Go mock module definition, source mode.
Specifies generated package name, instead of default one "mocks"

### Macro [GO_MOCKGEN_REFLECT](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1232) <a name="macro_GO_MOCKGEN_REFLECT"></a>

```ya.make
GO_MOCKGEN_REFLECT()
```

Part of Go mock module definition, reflect mode.
Creates generator program, expected in `gen` folder

### Macro [GO_MOCKGEN_SOURCE](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1287) <a name="macro_GO_MOCKGEN_SOURCE"></a>

```ya.make
GO_MOCKGEN_SOURCE(FILE, ARGS[], IN_NOPARSE[])
```

Part of Go mock module definition, source mode.
Generates mocks from file from GO_MOCKGEN_FROM or GO_MOCKGEN_CONTRIB_FROM
Can be placed multiple times in same ya.make

### Macro [GO_MOCKGEN_TYPES](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1213) <a name="macro_GO_MOCKGEN_TYPES"></a>

```ya.make
GO_MOCKGEN_TYPES(Types...)
```

_Not documented yet._

### Macro [GO_OAPI_CODEGEN](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1303) <a name="macro_GO_OAPI_CODEGEN"></a>

```ya.make
GO_OAPI_CODEGEN(GENERATE, PACKAGE, IN, IN_NOPARSE[], Args...)
```

Go oapi-codegen module
Generates GENERATE thing with PACKAGE package from file IN into STDOUT file
Optional arguments will be passed into generator
IN_NOPARSE - input files required for running generation, except IN
Can be placed multiple times in same ya.make

### Macro [GO_OAPI_CODEGEN_TAXI](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1326) <a name="macro_GO_OAPI_CODEGEN_TAXI"></a>

private, taxi only

### Macro [GO_OAPI_CODEGEN_TAXI_1134](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1332) <a name="macro_GO_OAPI_CODEGEN_TAXI_1134"></a>

private, taxi only

### Macro [GO_OAPI_CODEGEN_V2](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1320) <a name="macro_GO_OAPI_CODEGEN_V2"></a>

```ya.make
GO_OAPI_CODEGEN_V2(GENERATE, PACKAGE, IN, IN_NOPARSE[], Args...)
```

Go oapi-codegen (v2) module
Generates GENERATE thing with PACKAGE package from file IN into STDOUT file
Optional arguments will be passed into generator
IN_NOPARSE - input files required for running generation, except IN
Can be placed multiple times in same ya.make

PEERDIRs to dependencies of the generated code must be added manually.
All possible dependencies are listed in:
vendor/github.com/oapi-codegen/oapi-codegen/v2/pkg/codegen/templates/imports.tmpl
For example, see devtools/dummy_arcadia/go/oapi-codegen-v2

### Macro [GO_PACKAGE_NAME](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L385) <a name="macro_GO_PACKAGE_NAME"></a>

```ya.make
GO_PACKAGE_NAME(Name)
```

Override name of a Go package.

### Macro [GO_PROTO_PLUGIN](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L412) <a name="macro_GO_PROTO_PLUGIN"></a>

```ya.make
GO_PROTO_PLUGIN(Name Ext Tool [DEPS dependencies...])
```

Define protoc plugin for GO with given Name that emits extra output with provided extension
Ext using Tool. Extra dependencies are passed via DEPS.

### Macro [GO_PROTO_USE_V2](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L648) <a name="macro_GO_PROTO_USE_V2"></a>

```ya.make
GO_PROTO_USE_V2()
```

_Not documented yet._

### Macro [GO_SKIP_TESTS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L481) <a name="macro_GO_SKIP_TESTS"></a>

```ya.make
GO_SKIP_TESTS(TestNames...)
```

Define a set of tests that should not be run.
NB! Subtests are not taken into account!

### Macro [GO_SSO](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L219) <a name="macro_GO_SSO"></a>

```ya.make
GO_SSO(Command...)
```

_Not documented yet._

### Macro [GO_SSO_TOOL](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L232) <a name="macro_GO_SSO_TOOL"></a>

```ya.make
GO_SSO_TOOL(Tool...)
```

_Not documented yet._

### Macro [GO_TEST_EMBED_BINDIR](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L584) <a name="macro_GO_TEST_EMBED_BINDIR"></a>

```ya.make
GO_TEST_EMBED_BINDIR(DIR)
```

Define an embed directory DIR for files from ARCADIA_BUILD_ROOT for internal go tests

### Macro [GO_TEST_EMBED_PATTERN](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L515) <a name="macro_GO_TEST_EMBED_PATTERN"></a>

```ya.make
GO_TEST_EMBED_PATTERN(PATTERN)
```

Define an embed pattern for internal go tests.

### Macro [GO_TEST_FOR](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_GO_TEST_FOR"></a>

```ya.make
GO_TEST_FOR(path/to/module)  #builtin
```

Produces go test for specified module

### Macro [GO_TEST_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L420) <a name="macro_GO_TEST_SRCS"></a>

```ya.make
GO_TEST_SRCS(Files...)
```

.go sources for internal tests of a module

### Macro [GO_XTEST_EMBED_BINDIR](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L592) <a name="macro_GO_XTEST_EMBED_BINDIR"></a>

```ya.make
GO_XTEST_EMBED_BINDIR(DIR, FILES...)
```

Define an embed directory DIR for files from ARCADIA_BUILD_ROOT for external go tests

### Macro [GO_XTEST_EMBED_PATTERN](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L523) <a name="macro_GO_XTEST_EMBED_PATTERN"></a>

```ya.make
GO_XTEST_EMBED_PATTERN(PATTERN)
```

Define an embed pattern for external go tests.

### Macro [GO_XTEST_SRCS](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L430) <a name="macro_GO_XTEST_SRCS"></a>

```ya.make
GO_XTEST_SRCS(Files...)
```

.go sources for external tests of a module

### Macro [GRPC](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L620) <a name="macro_GRPC"></a>

```ya.make
GRPC()
```

Emit GRPC code for all .proto files in a PROTO_LIBRARY.
This works for all available PROTO_LIBRARY versions (C++, Python 2.x, Python 3.x, Java and Go).

### Macro [GRPC_WITH_GMOCK](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L641) <a name="macro_GRPC_WITH_GMOCK"></a>

```ya.make
GRPC_WITH_GMOCK()
```

Enable generating *_mock.grpc.pb.cc/h files

### Macro [HEADERS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5764) <a name="macro_HEADERS"></a>

```ya.make
HEADERS(<Dirs...> [EXCLUDE patterns...])
```

Add all C/C++ header files (h|H|hh|hpp|hxx|ipp) in given directories to SRCS
Exclude files matching EXCLUDE patterns

### Macro [IDEA_EXCLUDE_DIRS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2004) <a name="macro_IDEA_EXCLUDE_DIRS"></a>

```ya.make
IDEA_EXCLUDE_DIRS(<excluded dirs>)
```

Exclude specified directories from an idea project generated by ya ide idea
Have no effect on regular build.

### Macro [IDEA_MODULE_NAME](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2024) <a name="macro_IDEA_MODULE_NAME"></a>

```ya.make
IDEA_MODULE_NAME(module_name)
```

Set module name in an idea project generated by ya ide idea
Have no effect on regular build.

### Macro [IDEA_RESOURCE_DIRS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2014) <a name="macro_IDEA_RESOURCE_DIRS"></a>

```ya.make
IDEA_RESOURCE_DIRS(<additional dirs>)
```

Set specified resource directories in an idea project generated by ya ide idea
Have no effect on regular build.

### Macro [IF](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_IF"></a>

```ya.make
IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin
```

Apply macros if condition holds

### Macro [INCLUDE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_INCLUDE"></a>

```ya.make
INCLUDE(filename)  #builtin
```

Include file textually and process it as a part of the ya.make

### Macro [INCLUDE_ONCE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_INCLUDE_ONCE"></a>

```ya.make
INCLUDE_ONCE([yes|no])  #builtin
```

Control how file is is processed if it is included into one base ya.make by multiple paths.
if `yes` passed or argument omitted, process it just once. Process each time if `no` is passed (current default)
**Note:** for includes from multimodules the file is processed once from each submodule (like if INCLUDEs were preprocessed into multimodule body)

### Macro [INCLUDE_TAGS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_INCLUDE_TAGS"></a>

```ya.make
INCLUDE_TAGS(tags...)  # builtin
```

Additionally instantiate from multimodule all variants with tags listed (overrides default)

### Macro [INDUCED_DEPS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_INDUCED_DEPS"></a>

```ya.make
INDUCED_DEPS(Extension Path...)  #builtin
```

States that files wih the Extension generated by the PROGRAM will depend on files in Path.
This only useful in PROGRAM and similar modules. It will be applied if the PROGRAM is used in RUN_PROGRAM macro.
All Paths specified must be absolute arcadia paths i.e. start with ${ARCADIA_ROOT} ${ARCADIA_BUILD_ROOT}, ${CURDIR} or ${BINDIR}.

### Macro [INJECT_PEERS](https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L84) <a name="macro_INJECT_PEERS"></a>

```ya.make
INJECT_PEERS()
```

_Not documented yet._

### Macro [IOS_APP_ASSETS_FLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5516) <a name="macro_IOS_APP_ASSETS_FLAGS"></a>

```ya.make
IOS_APP_ASSETS_FLAGS(Flags...)
```

_Not documented yet._

### Macro [IOS_APP_COMMON_FLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5510) <a name="macro_IOS_APP_COMMON_FLAGS"></a>

```ya.make
IOS_APP_COMMON_FLAGS(Flags...)
```

_Not documented yet._

### Macro [IOS_APP_SETTINGS](https://a.yandex-team.ru/arcadia/build/plugins/ios_app_settings.py?rev=20020720#L5) <a name="macro_IOS_APP_SETTINGS"></a>

_Not documented yet._

### Macro [IOS_ASSETS](https://a.yandex-team.ru/arcadia/build/plugins/ios_assets.py?rev=20020720#L6) <a name="macro_IOS_ASSETS"></a>

_Not documented yet._

### Macro [IWYU_MAPPING_FILE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4502) <a name="macro_IWYU_MAPPING_FILE"></a>

```ya.make
IWYU_MAPPING_FILE(mapping_file)
```

Specify a mapping file for IWYU to provide custom mappings for headers.

### Macro [JAR_ANNOTATION_PROCESSOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L640) <a name="macro_JAR_ANNOTATION_PROCESSOR"></a>

```ya.make
JAR_ANNOTATION_PROCESSOR(Classes...)
```

_Not documented yet._

### Macro [JAR_EXCLUDE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2398) <a name="macro_JAR_EXCLUDE"></a>

```ya.make
JAR_EXCLUDE(Filters...)
```

Filter .jar file content: remove matched files
* and ** patterns are supported (like JAVA_SRCS)

### Macro [JAR_MAIN_CLASS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1146) <a name="macro_JAR_MAIN_CLASS"></a>

```ya.make
JAR_MAIN_CLASS(Class)
```

_Not documented yet._

### Macro [JAR_RESOURCE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L734) <a name="macro_JAR_RESOURCE"></a>

```ya.make
JAR_RESOURCE(Id)
```

_Not documented yet._

### Macro [JAVAC_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2035) <a name="macro_JAVAC_FLAGS"></a>

```ya.make
JAVAC_FLAGS(Args...)
```

Set additional Java compilation flags.

### Macro [JAVA_DEPENDENCIES_CONFIGURATION](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2387) <a name="macro_JAVA_DEPENDENCIES_CONFIGURATION"></a>

```ya.make
JAVA_DEPENDENCIES_CONFIGURATION(Vetos...)
```

Validate contrib/java dependencies
Valid arguments
FORBID_DIRECT_PEERDIRS - fail when module have direct PEERDIR (with version) (non-transitive)
FORBID_CONFLICT - fail when module have resolved without DEPENDENCY_MANAGEMENT version conflict (transitive)
FORBID_CONFLICT_DM - fail when module have resolved with DEPENDENCY_MANAGEMENT version conflict (transitive)
FORBID_CONFLICT_DM_RECENT - like FORBID_CONFLICT_DM but fail only when dependency have more recent version than specified in DEPENDENCY_MANAGEMENT
REQUIRE_DM - all dependencies must be specified in DEPENDENCY_MANAGEMENT (transitive)

### Macro [JAVA_EXTERNAL_DEPENDENCIES](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2356) <a name="macro_JAVA_EXTERNAL_DEPENDENCIES"></a>

```ya.make
JAVA_EXTERNAL_DEPENDENCIES(file1 file2 ...)
```

Add non-source java external build dependency (like lombok config file)

### Macro [JAVA_IGNORE_CLASSPATH_CLASH_FOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L612) <a name="macro_JAVA_IGNORE_CLASSPATH_CLASH_FOR"></a>

```ya.make
JAVA_IGNORE_CLASSPATH_CLASH_FOR([classes])
```

Ignore classpath clash test fails for classes

### Macro [JAVA_MODULE](https://a.yandex-team.ru/arcadia/build/plugins/java.py?rev=20020720#L41) <a name="macro_JAVA_MODULE"></a>

_Not documented yet._

### Macro [JAVA_PROTO_PLUGIN](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L217) <a name="macro_JAVA_PROTO_PLUGIN"></a>

```ya.make
JAVA_PROTO_PLUGIN(Name Tool DEPS <Dependencies>)
```

Define protoc plugin for Java with given Name that emits extra outputs
using Tool. Extra dependencies are passed via DEPS

### Macro [JAVA_RESOURCE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1040) <a name="macro_JAVA_RESOURCE"></a>

```ya.make
JAVA_RESOURCE(JAR, SOURCES="")
```

_Not documented yet._

### Macro [JAVA_RESOURCE_TAR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2140) <a name="macro_JAVA_RESOURCE_TAR"></a>

```ya.make
JAVA_RESOURCE_TAR(tar_path EXTRACT_ROOT root_dir)
```

Adds tar content as resources

**Documentation:** https://docs.yandex-team.ru/ya-make/manual/java/macros#java_resource_tar

### Macro [JAVA_SRCS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2128) <a name="macro_JAVA_SRCS"></a>

```ya.make
JAVA_SRCS(srcs)
```

Specify java source files and resources. A macro can be contained in any of four java modules.
**Keywords:**
1. SRCDIR x - specify the directory x is performed relatively to search the source code for these patterns. If there is no SRCDIR, the source will be searched relative to the module directory.
2. PACKAGE_PREFIX x - use if source paths relative to the SRCDIR does not coincide with the full class names. For example, if all sources of module are in the same package, you can create a directory package/name , and just put the source code in the SRCDIR and specify PACKAGE_PREFIX package.name.

**example:**
 - example/ya.make

       JAVA_PROGRAM()
           JAVA_SRCS(SRCDIR src/main/java **/*)
       END()

 - example/src/main/java/ru/yandex/example/HelloWorld.java

       package ru.yandex.example;
       public class HelloWorld {
            public static void main(String[] args) {
                System.out.println("Hello, World!");
            }
       }

**Documentation:** https://wiki.yandex-team.ru/yatool/java/#javasrcs

### Macro [JAVA_TEST](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) <a name="macro_JAVA_TEST"></a>

_Not documented yet._

### Macro [JAVA_TEST_DEPS](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) <a name="macro_JAVA_TEST_DEPS"></a>

_Not documented yet._

### Macro [JDK_VERSION](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2461) <a name="macro_JDK_VERSION"></a>

```ya.make
JDK_VERSION(Version)
```

Specify JDK version for module

### Macro [JNI_EXPORTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1324) <a name="macro_JNI_EXPORTS"></a>

```ya.make
JNI_EXPORTS()
```

_Not documented yet._

### Macro [JOIN_SRCS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3029) <a name="macro_JOIN_SRCS"></a>

```ya.make
JOIN_SRCS(Out Src...)
```

Join set of sources into single file named Out and send it for further processing.
This macro doesn't place all file into Out, it emits #include<Src>... Use the for C++ source files only.
You should specify file name with the extension as Out. Further processing will be done according this extension.

### Macro [JOIN_SRCS_GLOBAL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3040) <a name="macro_JOIN_SRCS_GLOBAL"></a>

```ya.make
JOIN_SRCS_GLOBAL(Out Src...)
```

Join set of sources into single file named Out and send it for further processing as if it were listed as SRCS(GLOBAL Out).
This macro doesn't place all file into Out, it emits #include<Src>... Use the for C++ source files only.
You should specify file name with the extension as Out. Further processing will be done according to this extension.

### Macro [JUNIT_TESTS_JAR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L182) <a name="macro_JUNIT_TESTS_JAR"></a>

```ya.make
JUNIT_TESTS_JAR(path/to/some/peer realname.jar)
```

Specifies jar to search test suites and test cases. By default tests are
searched in the jar compild by JTEST, JUNIT5 or JUNIT6 module sources. This macro
allows to specify diferent jar to search tests.

Only one jar file is used to search tests. If this macro invoked multiple
times (which is not reccomended practice) only the last invocation will
have effect.

If this macro is used no test from the module build by current ya.make
will be searched and executed.

### Macro [JVM_ARGS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1827) <a name="macro_JVM_ARGS"></a>

```ya.make
JVM_ARGS(Args...)
```

Arguments to run Java programs in tests.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/

### Macro [KAPT_ANNOTATION_PROCESSOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L863) <a name="macro_KAPT_ANNOTATION_PROCESSOR"></a>

```ya.make
KAPT_ANNOTATION_PROCESSOR(processors...)
```

Used to specify annotation processor qualified class names.
If specified multiple times, only last specification is used.

### Macro [KAPT_ANNOTATION_PROCESSOR_CLASSPATH](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L871) <a name="macro_KAPT_ANNOTATION_PROCESSOR_CLASSPATH"></a>

```ya.make
KAPT_ANNOTATION_PROCESSOR_CLASSPATH(jars...)
```

Used to specify classpath for annotation processors.
If specified multiple times, all specifications are used.

### Macro [KAPT_ANNOTATION_PROCESSOR_OPTIONS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L881) <a name="macro_KAPT_ANNOTATION_PROCESSOR_OPTIONS"></a>

```ya.make
KAPT_ANNOTATION_PROCESSOR_OPTIONS(Opts...)
```

Used to specify options for KAPT annotation processors.
If specified multiple times, all specifications are used.

### Macro [KAPT_OPTS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L855) <a name="macro_KAPT_OPTS"></a>

```ya.make
KAPT_OPTS(opts...)
```

Used to specify annotation processor qualified class names.
If specified multiple times, only last specification is used.

### Macro [KOTLINC_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2233) <a name="macro_KOTLINC_FLAGS"></a>

```ya.make
KOTLINC_FLAGS(-flags)
```

Set additional Kotlin compilation flags.

### Macro [KTLINT_BASELINE_FILE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2496) _(deprecated)_ <a name="macro_KTLINT_BASELINE_FILE"></a>

```ya.make
KTLINT_BASELINE_FILE(ktlint-baseline.xml "https://st.yandex-team.ru/REMOVE-BASELINE-1")
```

Path to baseline file for ktlint test and ticket to fix all ktlint warnings in file and then remove it

### Macro [KTLINT_RULESET](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2504) <a name="macro_KTLINT_RULESET"></a>

```ya.make
KTLINT_RULESET(path/to/ruleset/module)
```

Set path to ktlint ruleset module, used in command as "ktlint -R path/to/ruleset/module/ruletset-module.jar"

### Macro [LARGE_FILES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4906) <a name="macro_LARGE_FILES"></a>

```ya.make
LARGE_FILES([AUTOUPDATED]  Files...)
```

Use large file ether from working copy or from remote storage via placeholder <File>.external
If <File> is present locally (and not a symlink!) it will be copied to build directory.
Otherwise macro will try to locate <File>.external, parse it retrieve ot during build phase.

### Macro [LDFLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4271) <a name="macro_LDFLAGS"></a>

```ya.make
LDFLAGS(LinkerFlags...)
```

Add flags to the link command line of executable or shared library/dll.
**Note:** LDFLAGS are always global. When set in the LIBRARY module they will affect all programs/dlls/tests the library is linked into.
**Note:** remember about the incompatibility of flags for gcc and cl.

### Macro [LD_PLUGIN](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3426) <a name="macro_LD_PLUGIN"></a>

```ya.make
LD_PLUGIN(plugin_name)
```

Register script, which will process all inputs to any link_exe.py call with modules's library
Script will receive all arguments to link_exe.py, and can output into stdout preprocessed list
of all arguments, in JSON format

### Macro [LICENSE](https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L26) <a name="macro_LICENSE"></a>

```ya.make
LICENSE(licenses...)
```

Specify the licenses of the module, separated by spaces. Specifying multiple licenses interpreted as permission to use this
library satisfying all conditions of any of the listed licenses.

A license must be prescribed for contribs

### Macro [LICENSE_RESTRICTION](https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L43) <a name="macro_LICENSE_RESTRICTION"></a>

```ya.make
LICENSE_RESTRICTION(ALLOW_ONLY|DENY LicenseProperty...)
```

Restrict licenses of direct and indirect module dependencies.

ALLOW_ONLY restriction type requires dependent module to have at least one license without properties not listed in restrictions list.

DENY restriction type forbids dependency on module with no license without any listed property from the list.

**Note:** Can be used multiple times on the same module all specified constraints will be checked.
All macro invocation for the same module must use same constraints type (DENY or ALLOW_ONLY)

### Macro [LICENSE_RESTRICTION_EXCEPTIONS](https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L66) <a name="macro_LICENSE_RESTRICTION_EXCEPTIONS"></a>

```ya.make
LICENSE_RESTRICTION_EXCEPTIONS(Module...)
```

List of modules for exception from LICENSE_RESTRICTION and MODULEWISE_LICENSE_RESTRICTION logic.

### Macro [LICENSE_TEXTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5692) <a name="macro_LICENSE_TEXTS"></a>

```ya.make
LICENSE_TEXTS(File)
```

This macro specifies the filename with all library licenses texts

### Macro [LINKER_SCRIPT](https://a.yandex-team.ru/arcadia/build/plugins/linker_script.py?rev=20020720#L4) <a name="macro_LINKER_SCRIPT"></a>

```ya.make
LINKER_SCRIPT(Files...)
```

Specify files to be used as a linker script

### Macro [LINK_EXCLUDE_LIBRARIES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5824) <a name="macro_LINK_EXCLUDE_LIBRARIES"></a>

```ya.make
LINK_EXCLUDE_LIBRARIES(Libs...)
```

Exclude specified external dynamic libraries from linker command

May be used to implement shims/mocks, e.g. a lazy loader

**example:**

    LIBRARY()

    SRCS(
        # provide some shim/mock implementation for libcuda.so
    )

    LINK_EXCLUDE_LIBRARIES(cuda)

    END()

### Macro [LINT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1788) <a name="macro_LINT"></a>

```ya.make
LINT(<none|base|strict|extended>)
```

Set linting level for sources of the module

### Macro [LIST_PROTO](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L699) _(deprecated)_ <a name="macro_LIST_PROTO"></a>

```ya.make
LIST_PROTO([TO list.proto] Files...)
```

Create list of .proto files in a list-file (should be .proto, files.proto by default)
with original .proto-files as list's dependencies.

This allows to process files listed, passing list as an argument to the processor

**TODO:** proper implementation needed

### Macro [LJ_21_ARCHIVE](https://a.yandex-team.ru/arcadia/build/plugins/lj_archive.py?rev=20020720#L29) _(deprecated)_ <a name="macro_LJ_21_ARCHIVE"></a>

```ya.make
LJ_21_ARCHIVE(NAME Name LuaFiles...)
```

Precompile .lua files using LuaJIT 2.1 and archive both sources and results using sources names as keys

### Macro [LJ_ARCHIVE](https://a.yandex-team.ru/arcadia/build/plugins/lj_archive.py?rev=20020720#L4) <a name="macro_LJ_ARCHIVE"></a>

```ya.make
LJ_ARCHIVE(NAME Name LuaFiles...)
```

Precompile .lua files using LuaJIT and archive both sources and results using sources names as keys

### Macro [LLVM_BC](https://a.yandex-team.ru/arcadia/build/plugins/llvm_bc.py?rev=20020720#L5) <a name="macro_LLVM_BC"></a>

_Not documented yet._

### Macro [LLVM_COMPILE_C](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4995) <a name="macro_LLVM_COMPILE_C"></a>

```ya.make
LLVM_COMPILE_C(Input Output Opts...)
```

Emit LLVM bytecode from .c file. BC_CFLAGS, LLVM_OPTS and C_FLAGS_PLATFORM are passed in, while CFLAGS are not.
**Note:** Output name is used as is, no extension added.

### Macro [LLVM_COMPILE_CXX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4981) <a name="macro_LLVM_COMPILE_CXX"></a>

```ya.make
LLVM_COMPILE_CXX(Input Output Opts...)
```

Emit LLVM bytecode from .cpp file. BC_CXXFLAGS, LLVM_OPTS and C_FLAGS_PLATFORM are passed in, while CFLAGS are not.
**Note:** Output name is used as is, no extension added.

### Macro [LLVM_COMPILE_LL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5032) <a name="macro_LLVM_COMPILE_LL"></a>

```ya.make
LLVM_COMPILE_LL(Input Output Opts...)
```

Compile LLVM bytecode to object representation.
**Note:** Output name is used as is, no extension added.

### Macro [LLVM_LINK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5042) <a name="macro_LLVM_LINK"></a>

```ya.make
LLVM_LINK(Output Inputs...)
```

Call llvm-link on set of Inputs to produce Output.
**Note:** Unlike many other macros output argument goes first. Output name is used as is, no extension added.

### Macro [LLVM_LLC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5063) <a name="macro_LLVM_LLC"></a>

```ya.make
LLVM_LLC(Src Opts...)
```

Call llvm-llc with set of Opts on Src to produce object file.

**Note:** Output name is calculated as concatenation of Src name and platform specific object file extension.

### Macro [LLVM_OPT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5052) <a name="macro_LLVM_OPT"></a>

```ya.make
LLVM_OPT(Input Output Opts...)
```

Call llvm-opt with set of Opts on Input to produce Output.
**Note:** Output name is used as is, no extension added.

### Macro [LOCAL_JAR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L744) <a name="macro_LOCAL_JAR"></a>

```ya.make
LOCAL_JAR(File)
```

_Not documented yet._

### Macro [LOCAL_SOURCES_JAR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L749) <a name="macro_LOCAL_SOURCES_JAR"></a>

```ya.make
LOCAL_SOURCES_JAR(File)
```

_Not documented yet._

### Macro [MACROS_WITH_ERROR](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L8) <a name="macro_MACROS_WITH_ERROR"></a>

_Not documented yet._

### Macro [MANUAL_GENERATION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3393) <a name="macro_MANUAL_GENERATION"></a>

```ya.make
MANUAL_GENERATION(Outs...)
```

_Not documented yet._

### Macro [MASMFLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4296) <a name="macro_MASMFLAGS"></a>

```ya.make
MASMFLAGS(compiler flags)
```

Add the specified flags to the compilation command of .masm files.

### Macro [MAVEN_GROUP_ID](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2072) <a name="macro_MAVEN_GROUP_ID"></a>

```ya.make
MAVEN_GROUP_ID(group_id_for_maven_export)
```

Set maven export group id for JAVA_PROGRAM() and JAVA_LIBRARY().
Have no effect on regular build.

### Macro [MESSAGE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_MESSAGE"></a>

```ya.make
MESSAGE([severity] message)  # builtin
```

Print message with given severity level (STATUS, FATAL_ERROR)

### Macro [MODULEWISE_LICENSE_RESTRICTION](https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L58) <a name="macro_MODULEWISE_LICENSE_RESTRICTION"></a>

```ya.make
MODULEWISE_LICENSE_RESTRICTION(ALLOW_ONLY|DENY LicenseProperty...)
```

Restrict licenses per module only, without it peers.

ALLOW_ONLY restriction type requires module to have at least one license without properties not listed in restrictions list.

DENY restriction type forbids module with no license without any listed property from the list.

**Note:** Can be used multiple times on the same module all specified constraints will be checked.
All macro invocation for the same module must use same constraints type (DENY or ALLOW_ONLY)

### Macro [NEED_CHECK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4566) <a name="macro_NEED_CHECK"></a>

```ya.make
NEED_CHECK()
```

Commits to the project marked with this macro will be blocked by pre-commit check and then will be
automatically merged to trunk only if there is no new broken build targets in check results.
The use of this macro is disabled by default.

### Macro [NEED_REVIEW](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4584) _(deprecated)_ <a name="macro_NEED_REVIEW"></a>

```ya.make
NEED_REVIEW()
```

Mark the project as needing review.
Details can be found here: https://clubs.at.yandex-team.ru/arcadia/6104

### Macro [NGINX_MODULES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5669) <a name="macro_NGINX_MODULES"></a>

```ya.make
NGINX_MODULES(NGINX_PREFIX="contrib/nginx/core", Modules...)
```

_Not documented yet._

### Macro [NO_BUILD_IF](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_NO_BUILD_IF"></a>

```ya.make
NO_BUILD_IF([FATAL_ERROR|STRICT] variables)  # builtin
```

Print warning or error if some variable is true.
In STRICT mode disables build of all modules and RECURSES of the ya.make.
FATAL_ERROR issues configure error and enables STRICT mode.

### Macro [NO_CHECK_IMPORTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5160) <a name="macro_NO_CHECK_IMPORTS"></a>

```ya.make
NO_CHECK_IMPORTS([patterns])
```

Do not run checks on imports of Python modules.
Optional parameter mask patterns describes the names of the modules that do not need to check.

### Macro [NO_CLANG_COVERAGE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4474) <a name="macro_NO_CLANG_COVERAGE"></a>

```ya.make
NO_CLANG_COVERAGE()
```

Disable heavyweight clang coverage for the module. Clang coverage instrumentation is enabled by the --clang-coverage option.

### Macro [NO_CLANG_MCDC_COVERAGE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4482) <a name="macro_NO_CLANG_MCDC_COVERAGE"></a>

```ya.make
NO_CLANG_MCDC_COVERAGE()
```

Disable clang mc/dc instrumentation for the module. Clang mc/dc coverage instrumentation is enabled by the --clang-mcdc-coverage option.

### Macro [NO_CLANG_TIDY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4494) <a name="macro_NO_CLANG_TIDY"></a>

```ya.make
NO_CLANG_TIDY()
```

_Not documented yet._

### Macro [NO_COMPILER_WARNINGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4347) <a name="macro_NO_COMPILER_WARNINGS"></a>

```ya.make
NO_COMPILER_WARNINGS()
```

Disable all compiler warnings in the module.

### Macro [NO_COW](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L900) <a name="macro_NO_COW"></a>

```ya.make
NO_COW()
```

_Not documented yet._

### Macro [NO_CPU_CHECK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3172) <a name="macro_NO_CPU_CHECK"></a>

```ya.make
NO_CPU_CHECK()
```

Compile module without startup CPU features check

### Macro [NO_CUDA_NVPRUNE](https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L152) <a name="macro_NO_CUDA_NVPRUNE"></a>

```ya.make
NO_CUDA_NVPRUNE()
```

Disable nvprune for a PROGRAM

### Macro [NO_CYTHON_COVERAGE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1094) <a name="macro_NO_CYTHON_COVERAGE"></a>

```ya.make
NO_CYTHON_COVERAGE()
```

Disable cython and cythonized python coverage (CYTHONIZE_PY)
Implies NO_CLANG_COVERAGE() - right now, we can't disable instrumentation for .py.cpp files, but enable for .cpp

### Macro [NO_DEBUG_INFO](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4937) <a name="macro_NO_DEBUG_INFO"></a>

```ya.make
NO_DEBUG_INFO()
```

Compile files without debug info collection.

### Macro [NO_DOCTESTS](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L420) <a name="macro_NO_DOCTESTS"></a>

```ya.make
NO_DOCTESTS()
```

Disable doctests in PY[|3|23_]TEST

### Macro [NO_EXPORT_DYNAMIC_SYMBOLS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1331) <a name="macro_NO_EXPORT_DYNAMIC_SYMBOLS"></a>

```ya.make
NO_EXPORT_DYNAMIC_SYMBOLS()
```

Disable exporting all non-hidden symbols as dynamic when linking a PROGRAM.

### Macro [NO_EXTENDED_SOURCE_SEARCH](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L335) <a name="macro_NO_EXTENDED_SOURCE_SEARCH"></a>

```ya.make
NO_EXTENDED_SOURCE_SEARCH()
```

Prevent module using in extended python source search.
Use the macro if module contains python2-only files (or other python sources which shouldn't be imported by python3 interpreter)
which resides in the same directories with python 3 useful code. contrib/python/future is a example.
Anyway, preferred way is to move such files into separate dir and don't use this macro at all.

Also see: https://docs.yandex-team.ru/ya-make/manual/python/vars#y_python_extended_source_search for details

### Macro [NO_IMPORT_TRACING](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1085) <a name="macro_NO_IMPORT_TRACING"></a>

```ya.make
NO_IMPORT_TRACING()
```

Disable python coverage for module

### Macro [NO_IWYU](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4509) <a name="macro_NO_IWYU"></a>

```ya.make
NO_IWYU()
```

Disable Include What You Use (IWYU) analysis for the module.

### Macro [NO_JOIN_SRC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4446) _(deprecated)_ <a name="macro_NO_JOIN_SRC"></a>

```ya.make
NO_JOIN_SRC(), does-nothing
```

This macro currently does nothing. This is default behavior which cannot be overridden at module level.

### Macro [NO_LIBC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4400) <a name="macro_NO_LIBC"></a>

```ya.make
NO_LIBC()
```

Exclude dependencies on C++ and C runtimes (including util, musl and libeatmydata).
**Note:** use this with care. libc most likely will be linked into executable anyway,
so using libc headers/functions may not be detected at build time and may lead to unpredictable behavors at configure time.

### Macro [NO_LINT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1781) <a name="macro_NO_LINT"></a>

```ya.make
NO_LINT([ktlint])
```

Do not check for style files included in PY_SRCS, TEST_SRCS, JAVA_SRCS.
Ktlint can be disabled using NO_LINT(ktlint) explicitly.

### Macro [NO_LTO](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L402) <a name="macro_NO_LTO"></a>

```ya.make
NO_LTO()
```

Disable any lto (link-time optimizations) for the module.
This will compile module source files as usual (without LTO) but will not prevent lto-enabled
linking of entire program if global settings say so.

### Macro [NO_MYPY](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L510) <a name="macro_NO_MYPY"></a>

```ya.make
NO_MYPY()
```

_Not documented yet._

### Macro [NO_NEED_CHECK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4575) _(deprecated)_ <a name="macro_NO_NEED_CHECK"></a>

```ya.make
NO_NEED_CHECK()
```

Commits to the project marked with this macro will not be affected by higher-level NEED_CHECK macro.

### Macro [NO_OPTIMIZE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4338) <a name="macro_NO_OPTIMIZE"></a>

```ya.make
NO_OPTIMIZE()
```

Build code without any optimizations (-O0 mode).

### Macro [NO_OPTIMIZE_PY_PROTOS](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L164) <a name="macro_NO_OPTIMIZE_PY_PROTOS"></a>

```ya.make
NO_OPTIMIZE_PY_PROTOS()
```

Disable Python proto optimization using embedding corresponding C++ code into binary.
Python protobuf runtime will use C++ implementation instead of Python one if former is available.
This is default mode only for some system libraries.

### Macro [NO_PLATFORM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4411) <a name="macro_NO_PLATFORM"></a>

```ya.make
NO_PLATFORM()
```

Exclude dependencies on C++ and C runtimes (including util, musl and libeatmydata) and set NO_PLATFORM variable for special processing.
**Note:** use this with care. libc most likely will be linked into executable anyway,
so using libc headers/functions may not be detected at build time and may lead to unpredictable behavors at configure time.

### Macro [NO_PROFILE_RUNTIME](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4490) <a name="macro_NO_PROFILE_RUNTIME"></a>

```ya.make
NO_PROFILE_RUNTIME()
```

Never link this target with profile runtime. Only should be used for very basic build tools

### Macro [NO_PYTHON_COVERAGE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1077) <a name="macro_NO_PYTHON_COVERAGE"></a>

```ya.make
NO_PYTHON_COVERAGE()
```

Disable python coverage for module

### Macro [NO_RUNTIME](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4389) <a name="macro_NO_RUNTIME"></a>

```ya.make
NO_RUNTIME()
```

This macro:
1. Calls NO_UTIL()
2. If the project that contains the macro NO_RUNTIME(), peerdir-it project does not contain NO_RUNTIME() => Warning.
**Note:** use this with care. Arcadia STL most likely will be linked into executable anyway, so using STL headers/functions/classes
may not be detected at build time and may lead to unpredictable behavors at configure time.

### Macro [NO_SANITIZE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4454) <a name="macro_NO_SANITIZE"></a>

```ya.make
NO_SANITIZE()
```

Disable all sanitizers for the module.

### Macro [NO_SANITIZE_COVERAGE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4466) <a name="macro_NO_SANITIZE_COVERAGE"></a>

```ya.make
NO_SANITIZE_COVERAGE()
```

Disable lightweight coverage (-fsanitize-coverage) for the module.
Sanitize coverage is commonly used with fuzzing.
It might be useful to disable it for libraries that should never
be the main targets for fuzzing, like libfuzzer library itself.
Sanitize coverage instrumentation is enabled by the --sanitize-coverage option.

### Macro [NO_SPLIT_DWARF](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2752) <a name="macro_NO_SPLIT_DWARF"></a>

```ya.make
NO_SPLIT_DWARF()
```

Do NOT emit debug info for the PROGRAM/DLL as a separate file.
On macOS this also means do NOT generate dSym files (faster linkage)

### Macro [NO_SSE4](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3164) <a name="macro_NO_SSE4"></a>

```ya.make
NO_SSE4()
```

Compile module without SSE4

### Macro [NO_TS_TYPECHECK](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L300) <a name="macro_NO_TS_TYPECHECK"></a>

```ya.make
NO_TS_TYPECHECK()
```

_Not documented yet._

### Macro [NO_UTIL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4378) <a name="macro_NO_UTIL"></a>

```ya.make
NO_UTIL()
```

Build module without dependency on util.
**Note:** use this with care. Util most likely will be linked into executable anyway,
so using util headers/functions/classes may not be detected at build time and may lead to unpredictable behavors at configure time.

### Macro [NO_WSHADOW](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4354) <a name="macro_NO_WSHADOW"></a>

```ya.make
NO_WSHADOW()
```

Disable C++ shadowing warnings.

### Macro [NO_YMAKE_PYTHON3](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L269) <a name="macro_NO_YMAKE_PYTHON3"></a>

```ya.make
NO_YMAKE_PYTHON3()
```

_Not documented yet._

### Macro [NVCC_DEVICE_LINK](https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L143) <a name="macro_NVCC_DEVICE_LINK"></a>

```ya.make
NVCC_DEVICE_LINK(file.cu...)
```

Run nvcc --device-link on objects compiled from srcs with --device-c.
This generates a stub object devlink.o that supplies missing pieces for the
host linker to link relocatable device objects into the final executable.
This macro can be used only with [CUDA_DEVICE_LINK_LIBRARY](#module_CUDA_DEVICE_LINK_LIBRARY) module.

### Macro [OBJC_FLAGS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4325) <a name="macro_OBJC_FLAGS"></a>

```ya.make
OBJC_FLAGS(compiler_flags)
```

Add the specified flags to the compilation command of .mm files.

### Macro [ONLY_TAGS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_ONLY_TAGS"></a>

```ya.make
ONLY_TAGS(tags...)  # builtin
```

Instantiate from multimodule only variants with tags listed

### Macro [OPENSOURCE_EXPORT_REPLACEMENT](https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L83) <a name="macro_OPENSOURCE_EXPORT_REPLACEMENT"></a>

```ya.make
OPENSOURCE_EXPORT_REPLACEMENT(CMAKE PkgName CMAKE_COMPONENT OptCmakePkgComponent CMAKE_TARGET PkgName::PkgTarget CONAN ConanRequire CONAN ConanOptions CONAN_ADDITIONAL_SEMS ConanAdditionalSems)
```

Use specified conan/system package when exporting cmake build scripts for arcadia C++ project for opensource publication.

### Macro [OPENSOURCE_EXPORT_REPLACEMENT_BY_OS](https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L92) <a name="macro_OPENSOURCE_EXPORT_REPLACEMENT_BY_OS"></a>

```ya.make
OPENSOURCE_EXPORT_REPLACEMENT_BY_OS(OS Os CMAKE PkgName CMAKE_COMPONENT OptCmakePkgComponent CMAKE_TARGET PkgName::PkgTarget CONAN ConanRequire CONAN ConanOptions CONAN_ADDITIONAL_SEMS ConanAdditionalSems)
```

Use specified conan/system package when exporting cmake build scripts for arcadia C++ project for opensource publication.

### Macro [ORIGINAL_SOURCE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5679) <a name="macro_ORIGINAL_SOURCE"></a>

```ya.make
ORIGINAL_SOURCE(Source)
```

This macro specifies the source repository for contrib
Does nothing now (just a placeholder for future functionality)
See https://st.yandex-team.ru/DTCC-316

### Macro [PACK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2480) <a name="macro_PACK"></a>

```ya.make
PACK(archive_type)
```

When placed inside the PACKAGE module, packs the build results tree to the archive with specified extension. Currently supported extensions are `tar` and `tar.gz`

Is not allowed other module types than PACKAGE().

**See also:** [PACKAGE()](#module_PACKAGE)

### Macro [PARALLEL_TESTS_WITHIN_NODE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2939) <a name="macro_PARALLEL_TESTS_WITHIN_NODE"></a>

```ya.make
PARALLEL_TESTS_WITHIN_NODE(COUNT)
```

Execute tests in parallel within one node.

Supported for execution tests on yt/sandbox and local run. Ignored for unsupported test types and environments.
May cause some tests to break due to races or other circumstances.

Allowed values of COUNT:
'auto' to run tests in all available CPUs according to requirements, or integer amount of workers used.

EXPERIMENTAL! DO NOT USE IF YOU ARE NOT SURE.

### Macro [PARTITIONED_RECURSE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_PARTITIONED_RECURSE"></a>

```ya.make
PARTITIONED_RECURSE([BALANCING_CONFIG config] dirs...)  # builtin
```

Add directories to the build
All projects must be reachable from the root chain RECURSE() for monorepo continuous integration functionality.
Arguments are processed in chunks

### Macro [PARTITIONED_RECURSE_FOR_TESTS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_PARTITIONED_RECURSE_FOR_TESTS"></a>

```ya.make
PARTITIONED_RECURSE_FOR_TESTS([BALANCING_CONFIG config] dirs...)  # builtin
```

Add directories to the build if tests are demanded.
Arguments are processed in chunks

### Macro [PARTITIONED_RECURSE_ROOT_RELATIVE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_PARTITIONED_RECURSE_ROOT_RELATIVE"></a>

```ya.make
PARTITIONED_RECURSE_ROOT_RELATIVE([BALANCING_CONFIG config] dirlist)  # builtin
```

In comparison with RECURSE(), in dirlist there must be a directory relative to the root (${ARCADIA_ROOT}).
Arguments are processed in chunks

### Macro [PEERDIR](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_PEERDIR"></a>

```ya.make
PEERDIR(dirs...)  # builtin
```

Specify project dependencies
Indicates that the project depends on all of the projects from the list of dirs.
Libraries from these directories will be collected and linked to the current target if the target is executable or sharedlib/dll.
If the current target is a static library, the specified directories will not be built, but they will be linked to any executable target that will link the current library.
**params:**
1. As arguments PEERDIR you can only use the LIBRARY directory (the directory with the PROGRAM/DLL and derived from them are prohibited to use as arguments PEERDIR).
2. ADDINCL Keyword ADDINCL (written before the specified directory), adds the flag -I<path to library> the flags to compile the source code of the current project.
Perhaps it may be removed in the future (in favor of a dedicated ADDINCL)

### Macro [PIRE_INLINE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4152) <a name="macro_PIRE_INLINE"></a>

```ya.make
PIRE_INLINE(FILES...)
```

_Not documented yet._

### Macro [PIRE_INLINE_CMD](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4147) <a name="macro_PIRE_INLINE_CMD"></a>

```ya.make
PIRE_INLINE_CMD(SRC)
```

_Not documented yet._

### Macro [POPULATE_CPP_COVERAGE_FLAGS](https://a.yandex-team.ru/arcadia/build/conf/coverage_full_instrumentation.conf?rev=20020720#L7) <a name="macro_POPULATE_CPP_COVERAGE_FLAGS"></a>

```ya.make
POPULATE_CPP_COVERAGE_FLAGS()
```

_Not documented yet._

### Macro [POPULATE_CPP_YNDEXING](https://a.yandex-team.ru/arcadia/build/conf/yndexing/cpp_instrumentation.conf?rev=20020720#L6) <a name="macro_POPULATE_CPP_YNDEXING"></a>

```ya.make
POPULATE_CPP_YNDEXING()
```

_Not documented yet._

### Macro [PREPARE_INDUCED_DEPS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4677) <a name="macro_PREPARE_INDUCED_DEPS"></a>

```ya.make
PREPARE_INDUCED_DEPS(VAR Type Files...)
```

Format value for `INDUCED_DEPS` param in certain macros and assign to `VAR`
This tells that files of Type resulted from code generation macros (not neccessarily directly,
but in processing chain of generated files) should have extra dependencies from list of Files...

Prominent example here is Cython: one can generate .pyx file that may depend on .pxd and have cimpot from
certain .h. The former is dependency for .pyx itself, while the latter is dependency for .pyx.cpp
resulted from Cython-processing of generated pyx. The code ganeration will look like:
```
PREPARE_INDUCED_DEPS(PYX_DEPS pyx imported.pxd)
PREPARE_INDUCED_DEPS(CPP_DEPS cpp cdefed.h)
RUN_PYTHON3(generate_pyx.py genereted.pyx OUT generated.pyx INDUCED_DEPS $PYX_DEPS $CPP_DEPS)
```

The VAR will basically contain pair of `Type:[Files...]` in a form suitable for passing
as an element of array parameter. This is needed because language of ya.make doesn't support
Dict params right now and so it is impossible to directly pass something
like `{Type1:[Files2...], Type2:[Files2...]}`

### Macro [PROCESSOR_CLASSES](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L118) <a name="macro_PROCESSOR_CLASSES"></a>

```ya.make
PROCESSOR_CLASSES(Classes...)
```

_Not documented yet._

### Macro [PROCESS_DOCS](https://a.yandex-team.ru/arcadia/build/plugins/docs.py?rev=20020720#L41) <a name="macro_PROCESS_DOCS"></a>

_Not documented yet._

### Macro [PROCESS_MKDOCS](https://a.yandex-team.ru/arcadia/build/internal/plugins/mkdocs.py?rev=20020720#L38) <a name="macro_PROCESS_MKDOCS"></a>

_Not documented yet._

### Macro [PROTO2FBS](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L162) <a name="macro_PROTO2FBS"></a>

```ya.make
PROTO2FBS(InputProto)
```

Produce flatbuf schema out of protobuf description.

### Macro [PROTOC_FATAL_WARNINGS](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L144) <a name="macro_PROTOC_FATAL_WARNINGS"></a>

```ya.make
PROTOC_FATAL_WARNINGS()
```

Treat protoc warnings as fatal errors that break the build, for example, unused imports
Adds `--fatal_warnings` argument to protoc

### Macro [PROTO_ADDINCL](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L117) <a name="macro_PROTO_ADDINCL"></a>

```ya.make
PROTO_ADDINCL([GLOBAL] [WITH_GEN] Path)
```

This macro introduces proper ADDINCLs for .proto-files found in sources and
.cpp/.h generated files, supplying them to appropriate commands and allowing
proper dependency resolution at configure-time.

**Note:** you normally shouldn't use this macro. ADDINCLs should be sent to user
from dependency via PROTO_NAMESPACE macro

### Macro [PROTO_CMD](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1077) <a name="macro_PROTO_CMD"></a>

```ya.make
PROTO_CMD(SRC)
```

_Not documented yet._

### Macro [PROTO_NAMESPACE](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L134) <a name="macro_PROTO_NAMESPACE"></a>

```ya.make
PROTO_NAMESPACE([WITH_GEN] Namespace)
```

Defines protobuf namespace (import/export path prefix) which should be used for imports and
which defines output path for .proto generation.

For proper importing and configure-time dependency management it sets ADDINCLs
for both .cpp headers includes and .proto imports. If .proto expected to be used outside of the
processing module use GLOBAL to send proper ADDINCLs to all (transitive) users. PEERDIR to
PROTO_LIBRARY with PROTO_NAMESPACE() is enough at user side to correctly use the library.
If generated .proto files are going to be used for building a module than use of WITH_GEN
parameter will add appropriate dir from the build root for .proto files search.

### Macro [PROTO_TO_NAMESPACE](https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_proto.py?rev=20020720#L11) <a name="macro_PROTO_TO_NAMESPACE"></a>

```ya.make
PROTO_TO_NAMESPACE([IMPORT_ALL_IN_INIT] [IN_MSG|IN_PROTO] python_module ...)
```

Macro organizes generated proto modules from TOP_LEVEL into a specified Python namespace within a PY3_LIBRARY.
Use together with the PROTO_LIBRARY, for place generated modules to namespace.
Avoid using this macros! Prefer proper Python and proto imports instead. Reserve for legacy codebases.

**Parameters:**
  IMPORT_ALL_IN_INIT (optional) - generates __init__.py in the target subdirectory, importing all symbols from modules
  IN_MSG|IN_PROTO (optional, pick one) - Places modules in a subdirectory named `msg/` or `proto/` within the namespace.
                                         if not specified modules are placed directly in the namespace root.

  python_module - list of proto-generated Python modules (e.g., `agent_pb2`) to include.

**Example:**
    # path/to/proto_library/ya.make
    PROTO_LIBRARY()
        SRCS(agent.proto)
        PY_NAMESPACE(.)  # Generates agent_pb2 in TOP_LEVEL
    END()

    # ya.make with python library
    PY3_LIBRARY()
        PY_NAMESPACE(common_link)
        PROTO_TO_NAMESPACE(
            IMPORT_ALL_IN_INIT
            IN_MSG
            agent_pb2  # Generated module from PROTO_LIBRARY
        )
        PEERDIR(path/to/proto_library)
    END()

    # python code
    from common_link.msg import agent_pb2
    agent_type = agent_pb2.AgentType.Common

    from common_link.msg import AgentType  # AgentType is a message class generated from agent.proto
    agent_type = AgentType.Common

### Macro [PROVIDES](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_PROVIDES"></a>

```ya.make
PROVIDES(Name...)
```

Specifies provided features. The names must be correct C identifiers.
This prevents different libraries providing the same features to be linked into one program.

### Macro [PYTHON2_ADDINCL](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L944) <a name="macro_PYTHON2_ADDINCL"></a>

```ya.make
PYTHON2_ADDINCL()
```

This macro adds include path for Python headers (Python 2.x variant) without PEERDIR.
This should be used in 2 cases only:
- In PY2MODULE since it compiles into .so and uses external Python runtime;
- In system Python libraries themselves since proper PEERDIR there may create a loop;
In all other cases use USE_PYTHON2 macro instead.

Never use this macro in PY2_PROGRAM, PY2_LIBRARY and PY23_LIBRARY: they have everything needed by default.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs

### Macro [PYTHON2_MODULE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L631) <a name="macro_PYTHON2_MODULE"></a>

```ya.make
PYTHON2_MODULE()
```

Use in PY_ANY_MODULE to set it up for Python 2.x.

### Macro [PYTHON3_ADDINCL](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L993) <a name="macro_PYTHON3_ADDINCL"></a>

```ya.make
PYTHON3_ADDINCL()
```

This macro adds include path for Python headers (Python 3.x variant).
This should be used in 2 cases only:
- In PY2MODULE since it compiles into .so and uses external Python runtime;
- In system Python libraries themselves since peerdir there may create a loop;
In all other cases use USE_PYTHON3() macro instead.

Never use this macro in PY3_PROGRAM and PY3_LIBRARY and PY23_LIBRARY: they have everything by default.

**Documentation:** https://wiki.yandex-team.ru/devtools/commandsandvars/py_srcs

### Macro [PYTHON3_MODULE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L643) <a name="macro_PYTHON3_MODULE"></a>

```ya.make
PYTHON3_MODULE()
```

Use in PY_ANY_MODULE to set it up for Python 3.x.

### Macro [PYTHON_PATH](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1761) <a name="macro_PYTHON_PATH"></a>

```ya.make
PYTHON_PATH(Path)
```

Set path to Python that will be used to runs scripts in tests

### Macro [PY_CONSTRUCTOR](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L790) <a name="macro_PY_CONSTRUCTOR"></a>

```ya.make
PY_CONSTRUCTOR(package.module[:func])
```

Specifies the module or function which will be started before python's main()
init() is expected in the target module if no function is specified
Can be considered as __attribute__((constructor)) for python

### Macro [PY_DOCTESTS](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L701) <a name="macro_PY_DOCTESTS"></a>

```ya.make
PY_DOCTESTS(Packages...)
```

Add to the test doctests for specified Python packages
The packages should be part of a test (listed as sources of the test or its PEERDIRs).

### Macro [PY_ENUMS_SERIALIZATION](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L806) <a name="macro_PY_ENUMS_SERIALIZATION"></a>

_Not documented yet._

### Macro [PY_EXTRALIBS](https://a.yandex-team.ru/arcadia/build/plugins/extralibs.py?rev=20020720#L4) <a name="macro_PY_EXTRALIBS"></a>

```ya.make
PY_EXTRALIBS(liblist)
```

Add external dynamic libraries during program linkage stage" }

### Macro [PY_EXTRA_LINT_FILES](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1199) <a name="macro_PY_EXTRA_LINT_FILES"></a>

```ya.make
PY_EXTRA_LINT_FILES(files...)
```

Add extra Python files for linting. This macro allows adding
Python files which has no .py extension.

### Macro [PY_MAIN](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L767) <a name="macro_PY_MAIN"></a>

```ya.make
PY_MAIN(package.module[:func])
```

Specifies the module or function from which to start executing a python program

**Documentation:** https://wiki.yandex-team.ru/arcadia/python/pysrcs/#modulipyprogrampy3programimakrospymain

### Macro [PY_NAMESPACE](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L680) <a name="macro_PY_NAMESPACE"></a>

```ya.make
PY_NAMESPACE(prefix)
```

Sets default Python namespace for all python sources in the module.
Especially suitable in PROTO_LIBRARY where Python sources are generated and there is no PY_SRCS to place NAMESPACE parameter.

### Macro [PY_PROTOS_FOR](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_PY_PROTOS_FOR"></a>

```ya.make
PY_PROTOS_FOR(path/to/module)  #builtin, deprecated
```

Use PROTO_LIBRARY() in order to have .proto compiled into Python.
Generates pb2.py files out of .proto files and saves those into PACKAGE module

### Macro [PY_PROTO_PLUGIN](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L189) <a name="macro_PY_PROTO_PLUGIN"></a>

```ya.make
PY_PROTO_PLUGIN(Name Ext Tool DEPS <Dependencies>)
```

Define protoc plugin for python with given Name that emits extra output with provided Extension
using Tool. Extra dependencies are passed via DEPS

### Macro [PY_PROTO_PLUGIN2](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L201) <a name="macro_PY_PROTO_PLUGIN2"></a>

```ya.make
PY_PROTO_PLUGIN2(Name Ext1 Ext2 Tool DEPS <Dependencies>)
```

Define protoc plugin for python with given Name that emits 2 extra outputs with provided Extensions
using Tool. Extra dependencies are passed via DEPS

### Macro [PY_REGISTER](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L720) <a name="macro_PY_REGISTER"></a>

```ya.make
PY_REGISTER([package.]module_name)
```

Python knows about which built-ins can be imported, due to their registration in the Assembly or at the start of the interpreter.
All modules from the sources listed in PY_SRCS() are registered automatically.
To register the modules from the sources in the SRCS(), you need to use PY_REGISTER().

PY_REGISTER(module_name) initializes module globally via call to initmodule_name()
PY_REGISTER(package.module_name) initializes module in the specified package
It renames its init function with CFLAGS(-Dinitmodule_name=init7package11module_name)
or CFLAGS(-DPyInit_module_name=PyInit_7package11module_name)

**Documentation:** https://wiki.yandex-team.ru/arcadia/python/pysrcs/#makrospyregister

### Macro [PY_SRCS](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1125) <a name="macro_PY_SRCS"></a>

```ya.make
PY_SRCS({| CYTHON_C} { | TOP_LEVEL | NAMESPACE ns} Files...)
```

Build specified Python sources according to Arcadia binary Python build. Basically creates precompiled and source resources keyed with module paths.
The resources eventually are linked into final program and can be accessed as regular Python modules.
This custom loader linked into the program will add them to sys.meta_path.

PY_SRCS also support .proto, .ev, .pyx and .swg files. The .proto and .ev are compiled to .py-code by protoc and than handled as usual .py files.
.pyx and .swg lead to C/C++ Python extensions generation, that are automatically registered in Python as built-in modules.

By default .pyx files are built as C++-extensions. Use CYTHON_C to build them as C (similar to BUILDWITH_CYTHON_C, but with the ability to specify namespace).

__init__.py never required, but if present (and specified in PY_SRCS), it will be imported when you import package modules with __init__.py Oh.


PY_SRCS honors Python2 and Python3 differences and adjusts itself to Python version of a current module.
PY_SRCS can be used in any Arcadia Python build modules like PY*_LIBRARY, PY*_PROGRAM, PY*TEST.
PY_SRCS in LIBRARY or PROGRAM effectively converts these into PY2_LIBRARY and PY2_PROGRAM respectively.
It is strongly advised to make this conversion explicit. Never use PY_SRCS in a LIBRARY if you plan to use it from external Python extension module.

**Documentation:** https://wiki.yandex-team.ru/arcadia/python/pysrcs/#modulipylibrarypy3libraryimakrospysrcs

**Example:**

```ya.make
PY2_LIBRARY(mymodule)
    PY_SRCS(a.py sub/dir/b.py e.proto sub/dir/f.proto c.pyx sub/dir/d.pyx g.swg sub/dir/h.swg)
END()
```

### Macro [RECURSE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_RECURSE"></a>

```ya.make
RECURSE(dirs...)  # builtin
```

Add directories to the build
All projects must be reachable from the root chain RECURSE() for monorepo continuous integration functionality

### Macro [RECURSE_FOR_TESTS](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_RECURSE_FOR_TESTS"></a>

```ya.make
RECURSE_FOR_TESTS(dirs...)  # builtin
```

Add directories to the build if tests are demanded.
Use --force-build-depends flag if you want to build testing modules without tests running

### Macro [RECURSE_ROOT_RELATIVE](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_RECURSE_ROOT_RELATIVE"></a>

```ya.make
RECURSE_ROOT_RELATIVE(dirlist)  # builtin
```

In comparison with RECURSE(), in dirlist there must be a directory relative to the root (${ARCADIA_ROOT})

### Macro [REGISTER_SANDBOX_IMPORT](https://a.yandex-team.ru/arcadia/build/internal/plugins/sandbox_registry.py?rev=20020720#L6) <a name="macro_REGISTER_SANDBOX_IMPORT"></a>

_Not documented yet._

### Macro [REGISTER_YQL_PYTHON_UDF](https://a.yandex-team.ru/arcadia/build/plugins/yql_python_udf.py?rev=20020720#L4) <a name="macro_REGISTER_YQL_PYTHON_UDF"></a>

_Not documented yet._

### Macro [REQUIREMENTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1677) <a name="macro_REQUIREMENTS"></a>

```ya.make
REQUIREMENTS([cpu:<count>] [disk_usage:<size>] [ram:<size>] [ram_disk:<size>] [container:<id>] [network:<restricted|full>] [dns:dns64])
```

Allows you to specify the requirements of the test.

Documentation about the Arcadia test system: https://wiki.yandex-team.ru/yatool/test/

### Macro [REQUIRES](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L465) <a name="macro_REQUIRES"></a>

```ya.make
REQUIRES(dirs...)
```

Specify list of dirs which this module must depend on indirectly.

This macro can be used if module depends on the directories specified but they can't be listed
as direct PEERDIR dependencies (due to public include order or link order issues).

### Macro [REQUIRE_RESOURCE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1629) <a name="macro_REQUIRE_RESOURCE"></a>

```ya.make
REQUIRE_RESOURCE(PeerdirPath RESOURCES Resources...)
```

Specifies toolchain alike global resources which must be provided to a test.

Used only inside TEST modules.

### Macro [RESOLVE_PROTO](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L661) <a name="macro_RESOLVE_PROTO"></a>

```ya.make
RESOLVE_PROTO()
```

Enable include resolving within UNIONs and let system .proto being resolved
among .proto/.gztproto imports

**Note:** it is currently impossible to enable resolving only for .proto, so resolving is enabled for all supported files
also we only add ADDINCL for stock protobuf. So use this macro with care: it may cause resolving problems those are
to be addressed by either ADDINCLs or marking them as TEXT. Please contact devtools for details.

### Macro [RESOURCE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L524) <a name="macro_RESOURCE"></a>

```ya.make
RESOURCE([FORCE_TEXT ][Src Key]* [- Key=Value]*) # builtin
```

Add data (resources, random files, strings) to the program)
The common usage is to place Src file into binary. The Key is used to access it using library/cpp/resource or library/python/resource.
Alternative syntax with '- Key=Value' allows placing Value string as resource data into binary and make it accessible by Key.

This is a simpler but less flexible option than ARCHIVE(), because in the case of ARCHIVE(), you have to use the data explicitly,
and in the case of RESOURCE(), the data will fall through SRCS() or SRCS(GLOBAL) to binary linking.

Use the FORCE_TEXT parameter to explicitly mark all Src files as text files: they will not be parsed unless used elsewhere.

**example:** https://wiki.yandex-team.ru/yatool/howtowriteyamakefiles/#a2ispolzujjtekomanduresource

**example:**

    LIBRARY()
        RESOURCE(
            path/to/file1 /key/in/program/1
            path/to/file2 /key2
        )
    END()

### Macro [RESOURCE_FILES](https://a.yandex-team.ru/arcadia/build/plugins/res.py?rev=20020720#L12) <a name="macro_RESOURCE_FILES"></a>

```ya.make
RESOURCE_FILES([DONT_COMPRESS] [PREFIX {prefix}] [STRIP prefix_to_strip] {path})
```

This macro expands into
RESOURCE(DONT_PARSE {path} resfs/file/{prefix}{path}
    - resfs/src/resfs/file/{prefix}{remove_prefix(path, prefix_to_strip)}={rootrel_arc_src(path)}
)

resfs/src/{key} stores a source root (or build root) relative path of the
source of the value of the {key} resource.

resfs/file/{key} stores any value whose source was a file on a filesystem.
resfs/src/resfs/file/{key} must store its path.

DONT_COMPRESS allows optionally disable resource compression on platforms where it is supported

This form is for use from other plugins:
RESOURCE_FILES([DEST {dest}] {path}) expands into RESOURCE({path} resfs/file/{dest})

**See also:** https://wiki.yandex-team.ru/devtools/commandsandvars/resourcefiles/

### Macro [RESTRICT_PATH](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L14) <a name="macro_RESTRICT_PATH"></a>

_Not documented yet._

### Macro [RISK_GEN_DATA_MODEL](https://a.yandex-team.ru/arcadia/build/internal/plugins/fintech_risk_model.py?rev=20020720#L276) <a name="macro_RISK_GEN_DATA_MODEL"></a>

_Not documented yet._

### Macro [ROS_SRCS](https://a.yandex-team.ru/arcadia/build/internal/plugins/ros.py?rev=20020720#L5) <a name="macro_ROS_SRCS"></a>

```ya.make
ROS_SRCS(<[ZERO_COPY] File>...)
```

A helper macro for ROS .msg/.srv files

Add ZERO_COPY keyword before file name for zero-copy messages

### Macro [RUN](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L995) <a name="macro_RUN"></a>

_Not documented yet._

### Macro [RUN_ANTLR](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5247) <a name="macro_RUN_ANTLR"></a>

```ya.make
RUN_ANTLR(Args...)
```

Macro to invoke ANTLR3 generator (general case)

### Macro [RUN_ANTLR4](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5267) <a name="macro_RUN_ANTLR4"></a>

```ya.make
RUN_ANTLR4(Args...)
```

Macro to invoke ANTLR4 generator (general case)

### Macro [RUN_ANTLR4_CPP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5291) <a name="macro_RUN_ANTLR4_CPP"></a>

```ya.make
RUN_ANTLR4_CPP(GRAMMAR, OUTPUT_INCLUDES, LISTENER, VISITOR, Args...)
```

Macro to invoke ANTLR4 generator for combined lexer+parser grammars (Cpp)

### Macro [RUN_ANTLR4_CPP_SPLIT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5281) <a name="macro_RUN_ANTLR4_CPP_SPLIT"></a>

```ya.make
RUN_ANTLR4_CPP_SPLIT(LEXER, PARSER, OUTPUT_INCLUDES, LISTENER, VISITOR, Args...)
```

Macro to invoke ANTLR4 generator for separate lexer and parser grammars (Cpp)

### Macro [RUN_ANTLR4_GO](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5301) <a name="macro_RUN_ANTLR4_GO"></a>

```ya.make
RUN_ANTLR4_GO(GRAMMAR, DEPS <extra_go_deps>, LISTENER, VISITOR, Args...)
```

Macro to invoke ANTLR4 generator (Go)

### Macro [RUN_ANTLR4_PYTHON2](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5314) <a name="macro_RUN_ANTLR4_PYTHON2"></a>

```ya.make
RUN_ANTLR4_PYTHON2(Grammar [LISTENER] [VISITOR] [SUBDIR] [EXTRA_OUTS Outs...] Args...)
```

`LISTENER` - emit grammar listener
`VISITOR` -  emit grammar visitor
`SUBDIR` - place generated files to specified subdirectory of BINDIR
`EXTRA_OUTS` - list extra outputs produced by Antlr (e.g. .interp and .token files) if they are needed. If `SUBDIR` is specied it will affect these as well. Use file names only.

Macro to invoke ANTLR4 (version 4.11.1) generator (Python).

### Macro [RUN_ANTLR4_PYTHON3](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5327) <a name="macro_RUN_ANTLR4_PYTHON3"></a>

```ya.make
RUN_ANTLR4_PYTHON3(Grammar [LISTENER] [VISITOR] [SUBDIR] [EXTRA_OUTS Outs...] Args...)
```

`LISTENER` - emit grammar listener
`VISITOR` -  emit grammar visitor
`SUBDIR` - place generated files to specified subdirectory of BINDIR
`EXTRA_OUTS` - list extra outputs produced by Antlr (e.g. .interp and .token files) if they are needed. If `SUBDIR` is specied it will affect these as well. Use file names only.

Macro to invoke ANTLR4 generator (Python).

### Macro [RUN_JAVASCRIPT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L267) <a name="macro_RUN_JAVASCRIPT"></a>

```ya.make
RUN_JAVASCRIPT(script_path [args...] [IN inputs...] [OUTDIR outdir])
```

Run JS script after build of TS_* module.
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#run-javascript-after-build

### Macro [RUN_JAVASCRIPT_AFTER_BUILD](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L252) <a name="macro_RUN_JAVASCRIPT_AFTER_BUILD"></a>

```ya.make
RUN_JAVASCRIPT_AFTER_BUILD(script_path [args...] [IN inputs...] [OUTDIR outdir])
```

Run JS script after build of TS_* module.
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#run-javascript-after-build

### Macro [RUN_JAVA_PROGRAM](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L632) <a name="macro_RUN_JAVA_PROGRAM"></a>

```ya.make
RUN_JAVA_PROGRAM(Args...)
```

_Not documented yet._

### Macro [RUN_LUA](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4807) <a name="macro_RUN_LUA"></a>

```ya.make
RUN_LUA(script_path args... [CWD dir] [ENV key=value...] [TOOL tools...] [IN[_NOPARSE] inputs...] [OUT[_NOAUTO] outputs...] [STDOUT[_NOAUTO] output] [OUTPUT_INCLUDES output_includes...] [INDUCED_DEPS $VARs...])
```

Run a lua script.
These macros are similar: RUN_PROGRAM, RUN_LUA, PYTHON.

**Parameters:**
- script_path - Path to the script.3
- args... - Program arguments. Relative paths listed in TOOL, IN, OUT, STDOUT become absolute.
- CWD dir - Absolute path of the working directory.
- ENV key=value... - Environment variables.
- TOOL tools... - Auxiliary tool directories.
- IN[_NOPARSE] inputs... - Input files. NOPARSE inputs are treated as textual and not parsed for dependencies regardless of file extensions.
- OUT[_NOAUTO] outputs... - Output files. NOAUTO outputs are not automatically added to the build process.
- STDOUT[_NOAUTO] output - Redirect the standard output to the output file.
- OUTPUT_INCLUDES output_includes... - Includes of the output files that are needed to build them.
- INDUCED_DEPS $VARs... - Dependencies for generated files. Unlike `OUTPUT_INCLUDES` these may target files further in processing chain.
                          In order to do so VAR should be filled by PREPARE_INDUCED_DEPS macro, stating target files (by type) and set of dependencies

For absolute paths use ${ARCADIA_ROOT} and ${ARCADIA_BUILD_ROOT}, or
${CURDIR} and ${BINDIR} which are expanded where the outputs are used.

### Macro [RUN_PROGRAM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4781) <a name="macro_RUN_PROGRAM"></a>

```ya.make
RUN_PROGRAM(tool_path args... [CWD dir] [ENV key=value...] [TOOL tools...] [IN[_NOPARSE] inputs...] [OUT[_NOAUTO] outputs...] [STDOUT[_NOAUTO] output] [OUTPUT_INCLUDES output_includes...] [INDUCED_DEPS $VARs...])
```

Run a program from arcadia.
These macros are similar: RUN_PYTHON3, RUN_LUA, PYTHON.

**Parameters:**
- tool_path - Path to the directory of the tool.
- args... - Program arguments. Relative paths listed in TOOL, IN, OUT, STDOUT become absolute.
- CWD dir - Absolute path of the working directory.
- ENV key=value... - Environment variables.
- TOOL tools... - Auxiliary tool directories.
- IN[_NOPARSE] inputs... - Input files. NOPARSE inputs are treated as textual and not parsed for dependencies regardless of file extensions.
- OUT[_NOAUTO] outputs... - Output files. NOAUTO outputs are not automatically added to the build process.
- STDOUT[_NOAUTO] output - Redirect the standard output to the output file.
- OUTPUT_INCLUDES output_includes... - Includes of the output files that are needed to build them.
- INDUCED_DEPS $VARs... - Dependencies for generated files. Unlike `OUTPUT_INCLUDES` these may target files further in processing chain.
                          In order to do so VAR should be filled by PREPARE_INDUCED_DEPS macro, stating target files (by type) and set of dependencies

For absolute paths use ${ARCADIA_ROOT} and ${ARCADIA_BUILD_ROOT}, or
${CURDIR} and ${BINDIR} which are expanded where the outputs are used.
Note that Tool is always built for the host platform, so be careful to provide that tool can be built for all Arcadia major host platforms (Linux, MacOS and Windows).

### Macro [RUN_PY3_PROGRAM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4861) <a name="macro_RUN_PY3_PROGRAM"></a>

```ya.make
RUN_PY3_PROGRAM(tool_path args... [CWD dir] [ENV key=value...] [TOOL tools...] [IN[_NOPARSE] inputs...] [OUT[_NOAUTO] outputs...] [STDOUT[_NOAUTO] output] [OUTPUT_INCLUDES output_includes...] [INDUCED_DEPS $VARs...])
```

When build by ya make - Run a program from arcadia.
When exporting to other build systems (Cmake, Gradle, ...) - Run a python script __main__.py in tool project. Of course,
for exporting __main__.py must exists in tool project and must support execute by system Python3
These macros are similar: RUN_PROGRAM, RUN_PYTHON3, RUN_LUA, PYTHON.

**Parameters:**
- tool_path - Path to the directory of the tool.
- args... - Program arguments. Relative paths listed in TOOL, IN, OUT, STDOUT become absolute.
- CWD dir - Absolute path of the working directory.
- ENV key=value... - Environment variables.
- TOOL tools... - Auxiliary tool directories.
- IN[_NOPARSE] inputs... - Input files. NOPARSE inputs are treated as textual and not parsed for dependencies regardless of file extensions.
- OUT[_NOAUTO] outputs... - Output files. NOAUTO outputs are not automatically added to the build process.
- STDOUT[_NOAUTO] output - Redirect the standard output to the output file.
- OUTPUT_INCLUDES output_includes... - Includes of the output files that are needed to build them.
- INDUCED_DEPS $VARs... - Dependencies for generated files. Unlike `OUTPUT_INCLUDES` these may target files further in processing chain.
                          In order to do so VAR should be filled by PREPARE_INDUCED_DEPS macro, stating target files (by type) and set of dependencies

For absolute paths use ${ARCADIA_ROOT} and ${ARCADIA_BUILD_ROOT}, or
${CURDIR} and ${BINDIR} which are expanded where the outputs are used.
Note that Tool is always built for the host platform, so be careful to provide that tool can be built for all Arcadia major host platforms (Linux, MacOS and Windows).

### Macro [RUN_PYTHON3](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4833) <a name="macro_RUN_PYTHON3"></a>

```ya.make
RUN_PYTHON3(script_path args... [CWD dir] [ENV key=value...] [TOOL tools...] [IN[_NOPARSE] inputs...] [OUT[_NOAUTO] outputs...] [STDOUT[_NOAUTO] output] [OUTPUT_INCLUDES output_includes...] [INDUCED_DEPS $VARs...])
```

Run a python script with prebuilt python3 interpretor built from devtools/huge_python3.
These macros are similar: RUN_PROGRAM, RUN_LUA, PYTHON.

**Parameters:**
- script_path - Path to the script.
- args... - Program arguments. Relative paths listed in TOOL, IN, OUT, STDOUT become absolute.
- CWD dir - Absolute path of the working directory.
- ENV key=value... - Environment variables.
- TOOL tools... - Auxiliary tool directories.
- IN[_NOPARSE] inputs... - Input files. NOPARSE inputs are treated as textual and not parsed for dependencies regardless of file extensions.
- OUT[_NOAUTO] outputs... - Output files. NOAUTO outputs are not automatically added to the build process.
- OUT_GLOBAL outputs... - Global output files.
- STDOUT[_NOAUTO] output - Redirect the standard output to the output file.
- OUTPUT_INCLUDES output_includes... - Includes of the output files that are needed to build them.
- INDUCED_DEPS $VARs... - Dependencies for generated files. Unlike `OUTPUT_INCLUDES` these may target files further in processing chain.
                          In order to do so VAR should be filled by PREPARE_INDUCED_DEPS macro, stating target files (by type) and set of dependencies

For absolute paths use ${ARCADIA_ROOT} and ${ARCADIA_BUILD_ROOT}, or
${CURDIR} and ${BINDIR} which are expanded where the outputs are used.

### Macro [SDBUS_CPP_ADAPTOR](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5648) <a name="macro_SDBUS_CPP_ADAPTOR"></a>

```ya.make
SDBUS_CPP_ADAPTOR(File)
```

_Not documented yet._

### Macro [SDBUS_CPP_PROXY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5654) <a name="macro_SDBUS_CPP_PROXY"></a>

```ya.make
SDBUS_CPP_PROXY(File)
```

_Not documented yet._

### Macro [SDC_DIAGS_SPLIT_GENERATOR_V3](https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_diagnostics.py?rev=20020720#L61) <a name="macro_SDC_DIAGS_SPLIT_GENERATOR_V3"></a>

_Not documented yet._

### Macro [SDC_DIAGS_SPLIT_GENERATOR_V4](https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_diagnostics.py?rev=20020720#L24) <a name="macro_SDC_DIAGS_SPLIT_GENERATOR_V4"></a>

_Not documented yet._

### Macro [SDC_INSTALL](https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc.py?rev=20020720#L59) <a name="macro_SDC_INSTALL"></a>

```ya.make
SDC_INSTALL([Kind [Path | TARGET Target | NODE_LINK NodeName | NODE NodeName Target]...]...)
```

A helper macro to make sdc_install package layout in UNION.

Module name is used as a package name by default. This can be overriden by SDC_PACKAGE_NAME variable.

**Parameters:**
    - Kind - Controls the location of output.
      Should be one of COMMON_BIN, COMMON_LIB, ETC_ROS, INSTALL_ROOT, LAUNCH, LIB, PROFILE_HOOKS, PYTHON, SHARE, TS.
    - Path - Relative path to a source file to be bundled.
    - Target - Path to a target to be built and bundled.
    - NodeName - Name of a node for the link. The node should be part of the supernode.

**Example:**

    SDC_INSTALL(
        COMMON_BIN
            NODE_LINK node_starter
            TARGET sdg/sdc/ros/node_starter/yamakes/cgroup_dumper
            TARGET sdg/sdc/ros/node_starter/yamakes/setup_machine
        LAUNCH
            package.xml
            tests/launch/test.launch
        LIB
            NODE xml_diagnostics_republisher sdg/sdc/ros/node_starter/yamakes/xml_diagnostics_republisher_bin
    )

About NODE rule: Only one thing is deployed: either node binary, either supernode link. This is defined by
SDC_USE_SUPERNODE build flag.

### Macro [SELECT_CLANG_SA_CONFIG](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L167) <a name="macro_SELECT_CLANG_SA_CONFIG"></a>

```ya.make
SELECT_CLANG_SA_CONFIG(static_analyzer.yaml)
```

Select config file for clang static analyzer.
The file should be called static_analyzer.yaml.

### Macro [SELECT_PROTO_LAYOUT](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L82) <a name="macro_SELECT_PROTO_LAYOUT"></a>

```ya.make
SELECT_PROTO_LAYOUT(PROTO_LAYOUT)
```

Select proto layout for generation. Options repeated options from generator.cc (optimize for)

### Macro [SET](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SET"></a>

```ya.make
SET(varname value)  #builtin
```

Sets varname to value

### Macro [SETUP_EXECTEST](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) <a name="macro_SETUP_EXECTEST"></a>

_Not documented yet._

### Macro [SETUP_PYTEST_BIN](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L987) <a name="macro_SETUP_PYTEST_BIN"></a>

_Not documented yet._

### Macro [SETUP_RUN_PYTHON](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L1041) <a name="macro_SETUP_RUN_PYTHON"></a>

_Not documented yet._

### Macro [SET_APPEND](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SET_APPEND"></a>

```ya.make
SET_APPEND(varname appendvalue)  #builtin
```

Appends appendvalue to varname's value using space as a separator

### Macro [SET_APPEND_WITH_GLOBAL](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SET_APPEND_WITH_GLOBAL"></a>

```ya.make
SET_APPEND_WITH_GLOBAL(varname appendvalue)  #builtin
```

Appends appendvalue to varname's value using space as a separator.
New value is propagated to dependants

### Macro [SET_COMPILE_OUTPUTS_MODIFIERS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3192) <a name="macro_SET_COMPILE_OUTPUTS_MODIFIERS"></a>

```ya.make
SET_COMPILE_OUTPUTS_MODIFIERS(NOREL?"norel;output":"output")
```

_Not documented yet._

### Macro [SET_CPP_COVERAGE_FLAGS](https://a.yandex-team.ru/arcadia/build/plugins/coverage.py?rev=20020720#L43) <a name="macro_SET_CPP_COVERAGE_FLAGS"></a>

_Not documented yet._

### Macro [SET_CUSTOM_CLANG_TIDY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1189) <a name="macro_SET_CUSTOM_CLANG_TIDY"></a>

```ya.make
SET_CUSTOM_CLANG_TIDY(resource_module_path, var_name)
```

_Not documented yet._

### Macro [SET_RESOURCE_MAP_FROM_JSON](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SET_RESOURCE_MAP_FROM_JSON"></a>

```ya.make
SET_RESOURCE_MAP_FROM_JSON(VarName, FileName)
```

Loads the platform to resource uri mapping from the json file FileName and assign it to the variable VarName.
'VarName' value format is the same as an input of the DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE macro and can be passed to this macro as is.
File 'FileName' contains json with a 'canonized platform -> resource uri' mapping.
The mapping file format see in SET_RESOURCE_URI_FROM_JSON description.

### Macro [SET_RESOURCE_URI_FROM_JSON](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SET_RESOURCE_URI_FROM_JSON"></a>

```ya.make
SET_RESOURCE_URI_FROM_JSON(VarName, FileName)
```

Assigns a resource uri matched with a current target platform to the variable VarName.
The 'platform to resource uri' mapping is loaded from json file 'FileName'. File content example:
{
    "by_platform": {
        "linux": {
            "uri": "sbr:12345"
        },
        "darwin": {
            "uri": "sbr:54321"
        }
    }
}

### Macro [SIZE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3020) <a name="macro_SIZE"></a>

```ya.make
SIZE(SMALL/MEDIUM/LARGE)
```

Set the 'size' for the test. Each 'size' has own set of resrtictions, SMALL bein the most restricted and LARGE being the list.
See documentation on test system for more details.

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [SKIP_TEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1770) <a name="macro_SKIP_TEST"></a>

```ya.make
SKIP_TEST(Reason)
```

Skip the suite defined by test module. Provide a reason to be output in test execution report.

### Macro [SOURCE_GROUP](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SOURCE_GROUP"></a>

```ya.make
SOURCE_GROUP(...)  #builtin, deprecated
```

Ignored

### Macro [SPLIT_CODEGEN](https://a.yandex-team.ru/arcadia/build/internal/plugins/split_codegen.py?rev=20020720#L10) <a name="macro_SPLIT_CODEGEN"></a>

```ya.make
SPLIT_CODEGEN(tool prefix opts... [OUT_NUM num] [OUTPUT_INCLUDES output_includes...])
```

Generator of a certain number of parts of the .cpp file + one header .h file from .in

Supports keywords:
1. OUT_NUM <the number of generated Prefix.N.cpp default 25 (N varies from 0 to 24)>
2. OUTPUT_INCLUDES <path to files that will be included in generalnyj of macro files>

### Macro [SPLIT_DWARF](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2744) <a name="macro_SPLIT_DWARF"></a>

```ya.make
SPLIT_DWARF()
```

Emit debug info for the PROGRAM/DLL as a separate file <module_name>.debug.
**NB:** It does not help you to save process RSS but can add problems (see e.g. BEGEMOT-2147).

### Macro [SPLIT_FACTOR](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2984) <a name="macro_SPLIT_FACTOR"></a>

```ya.make
SPLIT_FACTOR(x)
```

Sets the number of chunks for parallel run tests when used in test module with FORK_TESTS() or FORK_SUBTESTS().
If none of those is specified this macro implies FORK_TESTS().

Supports C++ ut and PyTest.

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [SRC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3779) <a name="macro_SRC"></a>

```ya.make
SRC(File Flags...)
```

Compile single file with extra Flags.
Compilation is driven by the last extension of the File and Flags are specific to corresponding compilation command

### Macro [SRCDIR](https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16) <a name="macro_SRCDIR"></a>

```ya.make
SRCDIR(dirlist)  # builtin
```

Add the specified directories to the list of those in which the source files will be searched
Available only for arcadia/contrib

### Macro [SRCS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3798) <a name="macro_SRCS"></a>

```ya.make
SRCS(<[GLOBAL] File> ...)
```

Source files of the project. Files are built according to their extension and put int module output or fed to ultimate PROGRAM/DLL depending on GLOBAL presence.
Arcadia Paths from the root and is relative to the project's LIST are supported

GLOBAL marks next file as direct input to link phase of the program/shared library project built into. This prevents symbols of the file to be excluded by linker as unused.
The scope of the GLOBAL keyword is the following file (that is, in the case of SRCS(GLOBAL foo.cpp bar.cpp) global will be only foo.cpp)

**example:**

    LIBRARY(test_global)
        SRCS(GLOBAL foo.cpp)
    END()

This will produce foo.o and feed it to any PROGRAM/DLL module transitively depending on test_global library. The library itself will be empty and won't produce .a file.

### Macro [SRC_C_AMX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3933) <a name="macro_SRC_C_AMX"></a>

```ya.make
SRC_C_AMX(File Flags...)
```

Compile a single C/C++ file with AVX512 and additional Flags

### Macro [SRC_C_AVX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3909) <a name="macro_SRC_C_AVX"></a>

```ya.make
SRC_C_AVX(File Flags...)
```

Compile a single C/C++ file with AVX and additional Flags

### Macro [SRC_C_AVX2](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3917) <a name="macro_SRC_C_AVX2"></a>

```ya.make
SRC_C_AVX2(File Flags...)
```

Compile a single C/C++ file with AVX2 and additional Flags

### Macro [SRC_C_AVX512](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3925) <a name="macro_SRC_C_AVX512"></a>

```ya.make
SRC_C_AVX512(File Flags...)
```

Compile a single C/C++ file with AVX512 and additional Flags

### Macro [SRC_C_NO_LTO](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4007) <a name="macro_SRC_C_NO_LTO"></a>

```ya.make
SRC_C_NO_LTO(File Flags...)
```

Compile a single C/C++ file with link-time-optimization disabling and additional Flags

### Macro [SRC_C_PIC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3999) <a name="macro_SRC_C_PIC"></a>

```ya.make
SRC_C_PIC(File Flags...)
```

Compile a single C/C++ file with -fPIC and additional Flags

### Macro [SRC_C_SSE2](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3869) <a name="macro_SRC_C_SSE2"></a>

```ya.make
SRC_C_SSE2(File Flags...)
```

Compile a single C/C++ file with SSE2 and additional Flags

### Macro [SRC_C_SSE3](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3877) <a name="macro_SRC_C_SSE3"></a>

```ya.make
SRC_C_SSE3(File Flags...)
```

Compile a single C/C++ file with SSE3 and additional Flags

### Macro [SRC_C_SSE4](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3893) <a name="macro_SRC_C_SSE4"></a>

```ya.make
SRC_C_SSE4(File Flags...)
```

Compile a single C/C++ file with SSE4 and additional Flags

### Macro [SRC_C_SSE41](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3901) <a name="macro_SRC_C_SSE41"></a>

```ya.make
SRC_C_SSE41(File Flags...)
```

Compile a single C/C++ file with SSE4.1 and additional Flags

### Macro [SRC_C_SSSE3](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3885) <a name="macro_SRC_C_SSSE3"></a>

```ya.make
SRC_C_SSSE3(File Flags...)
```

Compile a single C/C++ file with SSSE3 and additional Flags

### Macro [SRC_C_XOP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3942) <a name="macro_SRC_C_XOP"></a>

```ya.make
SRC_C_XOP(File Flags...)
```

Compile a single C/C++ file with (an AMD-specific instruction set,
see https://en.wikipedia.org/wiki/XOP_instruction_set) and additional Flags

### Macro [SRC_RESOURCE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L739) <a name="macro_SRC_RESOURCE"></a>

```ya.make
SRC_RESOURCE(Id)
```

_Not documented yet._

### Macro [STRIP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4332) <a name="macro_STRIP"></a>

```ya.make
STRIP()
```

Strip debug info from a PROGRAM, DLL or TEST.
This macro doesn't work in LIBRARY's, UNION's and PACKAGE's.

### Macro [STYLE_CPP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5740) <a name="macro_STYLE_CPP"></a>

```ya.make
STYLE_CPP([CONFIG_TYPE config_type])
```

Run 'ya tool clang-format' test on all cpp sources and headers of the current module

### Macro [STYLE_DETEKT](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L52) <a name="macro_STYLE_DETEKT"></a>

```ya.make
STYLE_DETEKT([CONFIG_TYPE ct])
```

_Not documented yet._

### Macro [STYLE_DUMMY](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L376) <a name="macro_STYLE_DUMMY"></a>

```ya.make
STYLE_DUMMY()
```

Not an actual linter, used for dummy linter demonstration

### Macro [STYLE_FLAKE8](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L404) <a name="macro_STYLE_FLAKE8"></a>

```ya.make
STYLE_FLAKE8()
```

Check python3 sources for style issues using flake8.

### Macro [STYLE_JSON](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L13) <a name="macro_STYLE_JSON"></a>

```ya.make
STYLE_JSON([DIRS dirs] [DIRS_RECURSE dirs_recurse])
```

_ADD_PY_LINTER_CHECK(NAME name LINTER linter [DEPENDS deps] [DEFAULT_CONFIGS configs_file] [FILE_PROCESSING_TIME fpt] [EXTRA_PARAMS params] [CONFIG_TYPE ct])

### Macro [STYLE_PY2_FLAKE8](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L412) <a name="macro_STYLE_PY2_FLAKE8"></a>

```ya.make
STYLE_PY2_FLAKE8()
```

Check python3 sources for style issues using flake8.

### Macro [STYLE_PYTHON](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L384) <a name="macro_STYLE_PYTHON"></a>

```ya.make
STYLE_PYTHON([CONFIG_TYPE config_type])
```

Check python3 sources for style issues using black.

### Macro [STYLE_RUFF](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L395) <a name="macro_STYLE_RUFF"></a>

```ya.make
STYLE_RUFF([CONFIG_TYPE config_type] [CHECK_FORMAT])
```

Check python3 sources for style issues using ruff.
`CHECK_FORMAT` enables `ruff format` check.
`RUN_IN_SOURCE_ROOT` is a hacky option allowed for a limited number of projects, do not use it.

### Macro [STYLE_YAML](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L23) <a name="macro_STYLE_YAML"></a>

```ya.make
STYLE_YAML([DIRS dirs] [DIRS_RECURSE dirs_recurse])
```

_Not documented yet._

### Macro [STYLE_YQL](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L33) <a name="macro_STYLE_YQL"></a>

```ya.make
STYLE_YQL([DIRS dirs] [DIRS_RECURSE dirs_recurse])
```

_Not documented yet._

### Macro [SUBSCRIBER](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4598) <a name="macro_SUBSCRIBER"></a>

```ya.make
SUBSCRIBER(UsersOrGroups)
```

Add observers of the code.
In the SUBSCRIBER macro you can use:
1. login-s from staff.yandex-team.ru
2. Review group (to specify the Code-review group need to use the prefix g:)

**Note:** currently SUBSCRIBER is read only by Arcanum and is not processed by
the build system. It's planned to be phased out in favor of subcription via a.yaml

### Macro [SUPPRESSIONS](https://a.yandex-team.ru/arcadia/build/plugins/suppressions.py?rev=20020720#L4) <a name="macro_SUPPRESSIONS"></a>

SUPPRESSIONS() - allows to specify files with suppression notation which will be used by
address, leak or thread sanitizer runtime by default.
Use asan.supp filename for address sanitizer, lsan.supp for leak sanitizer,
ubsan.supp for undefined behavior sanitizer and tsan.supp for thread sanitizer
suppressions respectively.
See https://clang.llvm.org/docs/AddressSanitizer.html#suppressing-memory-leaks
for details.

### Macro [SYMLINK](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4649) <a name="macro_SYMLINK"></a>

```ya.make
SYMLINK(from to)
```

Add symlink

### Macro [SYSTEM_PROPERTIES](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1815) <a name="macro_SYSTEM_PROPERTIES"></a>

```ya.make
SYSTEM_PROPERTIES([<Key Value>...] [<File Path>...])
```

List of Key,Value pairs that will be available to test via System.getProperty().
FILE means that parst should be read from file specifies as Path.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/

### Macro [TAG](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1666) <a name="macro_TAG"></a>

```ya.make
TAG ([tag...])
```

Each test can have one or more tags used to filter tests list for running.
There are also special tags affecting test behaviour, for example ya:external, sb:ssd.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#obshhieponjatija

### Macro [TASKLET](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5346) <a name="macro_TASKLET"></a>

```ya.make
TASKLET()
```

_Not documented yet._

### Macro [TASKLET_REG](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5363) <a name="macro_TASKLET_REG"></a>

```ya.make
TASKLET_REG(Name, Lang, Impl, Includes...)
```

_Not documented yet._

### Macro [TASKLET_REG_EXT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5378) <a name="macro_TASKLET_REG_EXT"></a>

```ya.make
TASKLET_REG_EXT(Name, Lang, Impl, Wrapper, Includes...)
```

_Not documented yet._

### Macro [TEST_CWD](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2624) <a name="macro_TEST_CWD"></a>

```ya.make
TEST_CWD(path)
```

Defines working directory for test runs. Often used in conjunction with DATA() macro.
Is only used inside of the TEST modules.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/

### Macro [TEST_DATA](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L118) <a name="macro_TEST_DATA"></a>

_Not documented yet._

### Macro [TEST_JAVA_CLASSPATH_CMD_TYPE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2370) <a name="macro_TEST_JAVA_CLASSPATH_CMD_TYPE"></a>

```ya.make
TEST_JAVA_CLASSPATH_CMD_TYPE(Type)
```

Available types: MANIFEST(default), COMMAND_FILE, LIST
Method for passing a classpath value to a java command line
MANIFEST via empty jar file with manifest that contains Class-Path attribute
COMMAND_FILE via @command_file
LIST via flat args

### Macro [TEST_SRCS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1598) <a name="macro_TEST_SRCS"></a>

```ya.make
TEST_SRCS(Files...)
```

In PY2TEST, PY3TEST and PY*_LIBRARY modules used as PY_SRCS macro and additionally used to mine test cases to be executed by testing framework.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/#testynapytest

### Macro [THINLTO_CACHE](https://a.yandex-team.ru/arcadia/build/conf/linkers/ld.conf?rev=20020720#L434) <a name="macro_THINLTO_CACHE"></a>

```ya.make
THINLTO_CACHE(File)
```

Use specified file as cache for ThinLTO link phase either for reading or for writing
It is assumed that file is generated once in a while and than reused to accelerate ThinLTO builds

Default mode is reading and in this case file is consumed as input of link command in `--thinlto mode`
The most probable use is with `LARGE_FILES` or `FROM_SANDBOX` to bring cache from the storage.

In order to generate file use `-DTHINLTO_CACHE=gen` option and the file with name `out-<File>`
will be emitted as additional output of link command in `--thinlto` mode. The file name is mangled with `out-`
to avoid clashes with existing cache present in the build. Generated file is to be renamed and uploaded to the
storage, in case of using `FROM_SANDBOX` the resource ID is to be [auto-]updated upon upload.

### Macro [TIMEOUT](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2912) <a name="macro_TIMEOUT"></a>

```ya.make
TIMEOUT(TIMEOUT)
```

Sets a timeout on test execution

Documentation about the system test: https://wiki.yandex-team.ru/yatool/test/

### Macro [TOOLCHAIN](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5798) <a name="macro_TOOLCHAIN"></a>

Specify that current module is used as toolchain. Allows to have contrib hooks for toolchain modules
defined in repo internal python plugins

### Macro [TS_BIOME](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L370) <a name="macro_TS_BIOME"></a>

```ya.make
TS_BIOME(configFile)
```

For JavaScript, TypeScript, JSX, TSX, JSON, CSS and GraphQL. Must be inside of Module (TS_WEBPACK, TS_VITE, TS_NEXT, etc)

   - configFile by default biome.json

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-biome

**example:**

    TS_VITE()
        TS_BIOME(biome.json or biome.jsonc)
    END()

### Macro [TS_BUILD_ENV](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L276) <a name="macro_TS_BUILD_ENV"></a>

```ya.make
TS_BUILD_ENV(key=value)
```

Sets env variable key to value.

### Macro [TS_BUILD_OUTPUTS](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L66) <a name="macro_TS_BUILD_OUTPUTS"></a>

```ya.make
TS_BUILD_OUTPUTS(PATHS...)
```

_Not documented yet._

### Macro [TS_BUILD_SCRIPT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L62) <a name="macro_TS_BUILD_SCRIPT"></a>

```ya.make
TS_BUILD_SCRIPT(SCRIPT)
```

_Not documented yet._

### Macro [TS_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L36) <a name="macro_TS_CONFIG"></a>

```ya.make
TS_CONFIG(ConfigPath)
```

Macro sets the path for "TypeScript Config".

- ConfigPath - config path (one at least)

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-config

### Macro [TS_ESLINT_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L143) <a name="macro_TS_ESLINT_CONFIG"></a>

```ya.make
TS_ESLINT_CONFIG(ConfigPath)
```

Macro sets the path for ESLint config file.

- ConfigPath - config path

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-eslint-config

### Macro [TS_EXCLUDE_FILES_GLOB](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L101) <a name="macro_TS_EXCLUDE_FILES_GLOB"></a>

```ya.make
TS_EXCLUDE_FILES_GLOB(GlobExpression)
```

Macro sets glob to mark some files to ignore while building.
These files won't be copied to BINDIR.

- GlobExpression - glob expression

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-exclude-files-glob

### Macro [TS_FILES](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L219) <a name="macro_TS_FILES"></a>

```ya.make
TS_FILES(Files...)
```

Adds files to output as is. Does not add a command to copy the file to builddir.
Similar to FILES but works for TS build modules
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_PACKAGE#ts-files

### Macro [TS_FILES_GLOB](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L228) <a name="macro_TS_FILES_GLOB"></a>

```ya.make
TS_FILES_GLOB(Glob...)
```

Adds files to output by glob, e.g. TS_FILES_GLOB(**/*.css)
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_PACKAGE#ts-files-glob

### Macro [TS_LARGE_FILES](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L242) <a name="macro_TS_LARGE_FILES"></a>

```ya.make
TS_LARGE_FILES(DESTINATION dest_dir Files...)
```

Use large file ether from working copy or from remote storage via placeholder <File>.external
If <File> is present locally (and not a symlink!) it will be copied to build directory.
Otherwise macro will try to locate <File>.external, parse it and fetch the file during build phase.

Then file will be copied to DESTINATION folder preserving file structure.
Copied file becomes output of TS_PACKAGE
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_PACKAGE#ts-large-files

### Macro [TS_LINT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L8) <a name="macro_TS_LINT"></a>

```ya.make
TS_LINT(SCRIPT_NAME, TIMEOUT_MEDIUM?"yes":"no")
```

_Not documented yet._

### Macro [TS_NEXT_BUILD_OPTIONS](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L22) <a name="macro_TS_NEXT_BUILD_OPTIONS"></a>

```ya.make
TS_NEXT_BUILD_OPTIONS(Options...)
```

Macro sets the build options for TS_NEXT module.

- Options - next build options.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_NEXT#ts-next-build-options

### Macro [TS_NEXT_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L11) <a name="macro_TS_NEXT_CONFIG"></a>

```ya.make
TS_NEXT_CONFIG(ConfigPath)
```

Macro sets the config path for TS_NEXT module.

- ConfigPath - config path. Default value: next.config.js

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_NEXT#ts-next-config

### Macro [TS_NEXT_EXPERIMENTAL_BUILD_MODE](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L45) <a name="macro_TS_NEXT_EXPERIMENTAL_BUILD_MODE"></a>

```ya.make
TS_NEXT_EXPERIMENTAL_BUILD_MODE()
```

Macro tune the build command to use experimental build feature.
The method depends on the next.js version.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_NEXT#ts-next-experimental-build-mode

### Macro [TS_NEXT_OUTPUT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L35) <a name="macro_TS_NEXT_OUTPUT"></a>

```ya.make
TS_NEXT_OUTPUT(DirName)
```

Macro sets the output directory name for TS_NEXT module.

- DirName - output directory name. Default value: .next.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_NEXT#ts-next-output

### Macro [TS_PROTO_OPT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_proto.conf?rev=20020720#L88) <a name="macro_TS_PROTO_OPT"></a>

```ya.make
TS_PROTO_OPT(key1=value1 key2=value2)
```

Overrides default options for `--ts_proto_opt`
([supported options](https://github.com/stephenh/ts-proto?tab=readme-ov-file#supported-options)).

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_PROTO#ts_proto_opt

**Example:**

```ya.make
TS_PROTO_OPT(env=browser)
TS_PROTO_OPT(
    useJsonName=true
    useJsonWireFormat=true
)
```

### Macro [TS_PROTO_PACKAGE_NAME](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_proto.conf?rev=20020720#L103) <a name="macro_TS_PROTO_PACKAGE_NAME"></a>

```ya.make
TS_PROTO_PACKAGE_NAME(@scope/pkg)
```

Sets package name for `TS_PROTO`.
Use `@scope/*` to set package scope with autogenerated name.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_PROTO#ts_proto_package_name

**Example:**

```ya.make
TS_PROTO_PACKAGE_NAME(@yandex-proto/ci-tasklet-sidecar)
TS_PROTO_PACKAGE_NAME(@yandex-proto/*)
```

### Macro [TS_RSPACK_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L10) <a name="macro_TS_RSPACK_CONFIG"></a>

```ya.make
TS_RSPACK_CONFIG(ConfigPath)
```

Macro sets the config path for TS_RSPACK module.

- ConfigPath - config path

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_RSPACK#ts-rspack-config

### Macro [TS_RSPACK_OUTPUT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L22) <a name="macro_TS_RSPACK_OUTPUT"></a>

```ya.make
TS_RSPACK_OUTPUT(FirstDirName DirNames)
```

Macro sets the output directory names (one at least) for TS_RSPACK module.

- DirNames - output directory names (one at least)
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_RSPACK#ts-rspack-output

### Macro [TS_STYLELINT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L344) <a name="macro_TS_STYLELINT"></a>

```ya.make
TS_STYLELINT(configFile)
```

For check CSS, SASS, LESS for StyleLint. Must be inside of Module (TS_WEBPACK, TS_VITE, TS_NEXT, etc)

   - configFile - by default .stylelintrc.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-stylelint

**example:**

    TS_VITE()
        TS_STYLELINT(.stylelintrc)
    END()

### Macro [TS_TEST](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L12) <a name="macro_TS_TEST"></a>

```ya.make
TS_TEST(SCRIPT_NAME)
```

_Not documented yet._

### Macro [TS_TEST_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L234) <a name="macro_TS_TEST_CONFIG"></a>

```ya.make
TS_TEST_CONFIG(Path)
```

Macro sets the path to configuration file of the test runner.

- Path - path to the config file.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-test-config

### Macro [TS_TEST_DATA](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L275) <a name="macro_TS_TEST_DATA"></a>

```ya.make
TS_TEST_DATA([RENAME] GLOBS...)
```

Macro to add tests data (i.e. snapshots) used in testing to a bindir from curdir.
Creates symbolic links to directories of files found by the specified globs.

**Parameters:**
- RENAME - adds ability to rename paths for tests data from curdir to bindir.
           For example if your tested module located on "module" path and tests data in "module/tests_data".
           Then you can be able to rename "tests_data" folder to something else - `RENAME tests_data:example`.
           As a result in your bindir will be created folder - "module/example" which is a symbolic link on "module/tests_data" in curdir.
           It is possible to specify multiple renaming rules in the following format "dir1:dir2;dir3/foo:dir4/bar", where "dir1" and "dir3" folders in curdir.
- GLOBS... - globs to tests data files, symbolic links will be created to their folders. For example - "tests_data/**/*".

### Macro [TS_TEST_DEPENDS_ON_BUILD](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L284) <a name="macro_TS_TEST_DEPENDS_ON_BUILD"></a>

```ya.make
TS_TEST_DEPENDS_ON_BUILD()
```

Macro enables build and results unpacking for the module test is targeting.
It is not required for most of the tests, but it might be needeed in some special cases.

### Macro [TS_TEST_INCLUDE_NODEJS](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L291) <a name="macro_TS_TEST_INCLUDE_NODEJS"></a>

```ya.make
TS_TEST_INCLUDE_NODEJS()
```

Macro adds NodeJS binary to the test module build output.

### Macro [TS_TEST_SRCS](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L252) <a name="macro_TS_TEST_SRCS"></a>

```ya.make
TS_TEST_SRCS(DIRS...)
```

Macro to define directories where the test source files should be located.
It does not define the exact scope to run tests for - it is to define the scope for the
test module configuration.
For example, changes to test files in these directories will not cause the test to be re-run,
and results could be retrieved from the cache.

- DIRS... - directories.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-test-srcs

### Macro [TS_TYPECHECK](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L321) <a name="macro_TS_TYPECHECK"></a>

```ya.make
TS_TYPECHECK(tsconfigFile)
```

For check CSS, SASS, LESS for StyleLint. Must be inside of Module (TS_WEBPACK, TS_VITE, TS_NEXT, etc)

   - tsconfigFile - by default tsconfig.json or value from TS_CONFIG macros.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#ts-typecheck

**example:**

    TS_VITE()
        TS_TYPECHECK()
    END()

    TS_WEBPACK()
        TS_TYPECHECK(tsconfig.wp.json)
    END()

### Macro [TS_USE_BUN](https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L283) <a name="macro_TS_USE_BUN"></a>

```ya.make
TS_USE_BUN()
```

Adds `bun` binary for host platform.

### Macro [TS_VITE_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L10) <a name="macro_TS_VITE_CONFIG"></a>

```ya.make
TS_VITE_CONFIG(ConfigPath)
```

Macro sets the config path for TS_VITE module.

- ConfigPath - config path

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_VITE#ts-vite-config

### Macro [TS_VITE_OUTPUT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L24) <a name="macro_TS_VITE_OUTPUT"></a>

```ya.make
TS_VITE_OUTPUT(DirName)
```

Macro sets the output directory name for TS_VITE module.

- DirName - output directory name

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_VITE#ts-vite-output

### Macro [TS_WEBPACK_CONFIG](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L10) <a name="macro_TS_WEBPACK_CONFIG"></a>

```ya.make
TS_WEBPACK_CONFIG(ConfigPath)
```

Macro sets the config path for TS_WEBPACK module.

- ConfigPath - config path

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_WEBPACK#ts-webpack-config

### Macro [TS_WEBPACK_OUTPUT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L22) <a name="macro_TS_WEBPACK_OUTPUT"></a>

```ya.make
TS_WEBPACK_OUTPUT(FirstDirName DirNames)
```

Macro sets the output directory names (one at least) for TS_WEBPACK module.

- DirNames - output directory names (one at least)
**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/TS_WEBPACK#ts-webpack-output

### Macro [UBERJAR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1873) <a name="macro_UBERJAR"></a>

```ya.make
UBERJAR()
```

UBERJAR is a single all-in-one jar-archive that includes all its Java dependencies (reachable PEERDIR).
It also supports shading classes inside the archive by moving them to a different package (similar to the maven-shade-plugin).
Use UBERJAR inside JAVA_PROGRAM module.

You can use the following macros to configure the archive:
1. UBERJAR_HIDING_PREFIX prefix for classes to shade (classes remain in their packages by default)
2. UBERJAR_HIDE_INCLUDE_PATTERN include classes matching this patterns to shading, include LDC mapping
3. UBERJAR_HIDE_EXCLUDE_PATTERN exclude classes matching this patterns from shading (if enabled).
4. UBERJAR_PATH_EXCLUDE_PREFIX the prefix for classes that should not get into the jar archive (all classes are placed into the archive by default)
5. UBERJAR_MANIFEST_TRANSFORMER_MAIN add ManifestResourceTransformer class to uberjar processing and specify main-class
6. UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE add ManifestResourceTransformer class to uberjar processing and specify some attribute
7. UBERJAR_APPENDING_TRANSFORMER add AppendingTransformer class to uberjar processing
8. UBERJAR_SERVICES_RESOURCE_TRANSFORMER add ServicesResourceTransformer class to uberjar processing

**Documentation:** https://wiki.yandex-team.ru/yatool/java/

**See also:** [JAVA_PROGRAM](#module_JAVA_PROGRAM), [UBERJAR_HIDING_PREFIX](#macro_UBERJAR_HIDING_PREFIX), [UBERJAR_HIDE_INCLUDE_PATTERN](#macro_UBERJAR_HIDE_INCLUDE_PATTERN) [UBERJAR_HIDE_EXCLUDE_PATTERN](#macro_UBERJAR_HIDE_EXCLUDE_PATTERN), [UBERJAR_PATH_EXCLUDE_PREFIX](#macro_UBERJAR_PATH_EXCLUDE_PREFIX)

### Macro [UBERJAR_APPENDING_TRANSFORMER](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1980) <a name="macro_UBERJAR_APPENDING_TRANSFORMER"></a>

```ya.make
UBERJAR_APPENDING_TRANSFORMER(Resource)
```

Add AppendingTransformer for UBERJAR() java programs

**Parameters:**
- Resource - Resource name

**See also:** [UBERJAR](#macro_UBERJAR)

### Macro [UBERJAR_HIDE_EXCLUDE_PATTERN](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1909) <a name="macro_UBERJAR_HIDE_EXCLUDE_PATTERN"></a>

```ya.make
UBERJAR_HIDE_EXCLUDE_PATTERN(Args...)
```

Exclude classes matching this patterns from shading (if enabled).
Pattern may contain '*' and '**' globs.
Shading is enabled for UBERJAR program using UBERJAR_HIDING_PREFIX macro. If this macro is not specified all classes are shaded.

**See also:** [UBERJAR](#macro_UBERJAR), [UBERJAR_HIDING_PREFIX](#macro_UBERJAR_HIDING_PREFIX)

### Macro [UBERJAR_HIDE_INCLUDE_PATTERN](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1924) <a name="macro_UBERJAR_HIDE_INCLUDE_PATTERN"></a>

```ya.make
UBERJAR_HIDE_INCLUDE_PATTERN(Args...)
```

Include classes matching this patterns to shading, enabled LDC processing.
Pattern may contain '*' and '**' globs.
Shading is enabled for UBERJAR program using UBERJAR_HIDING_PREFIX macro. If this macro is not specified all classes are shaded.

**See also:** [UBERJAR](#macro_UBERJAR), [UBERJAR_HIDING_PREFIX](#macro_UBERJAR_HIDING_PREFIX)

### Macro [UBERJAR_HIDING_PREFIX](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1894) <a name="macro_UBERJAR_HIDING_PREFIX"></a>

```ya.make
UBERJAR_HIDING_PREFIX(Arg)
```

Set prefix for classes to shade. All classes in UBERJAR will be moved into package prefixed with Arg.
Classes remain in their packages by default.

**See also:** [UBERJAR](#macro_UBERJAR)

### Macro [UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1964) <a name="macro_UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE"></a>

```ya.make
UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE(Key, Value)
```

Transform manifest.mf for UBERJAR() java programs, set attribute

**See also:** [UBERJAR](#macro_UBERJAR)

### Macro [UBERJAR_MANIFEST_TRANSFORMER_MAIN](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1951) <a name="macro_UBERJAR_MANIFEST_TRANSFORMER_MAIN"></a>

```ya.make
UBERJAR_MANIFEST_TRANSFORMER_MAIN(Main)
```

Transform manifest.mf for UBERJAR() java programs, set main-class attribute

**See also:** [UBERJAR](#macro_UBERJAR)

### Macro [UBERJAR_PATH_EXCLUDE_PREFIX](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1938) <a name="macro_UBERJAR_PATH_EXCLUDE_PREFIX"></a>

```ya.make
UBERJAR_PATH_EXCLUDE_PREFIX(Args...)
```

Exclude classes matching this patterns from UBERJAR.
By default all dependencies of UBERJAR program will lend in a .jar archive.

**See also:** [UBERJAR](#macro_UBERJAR)

### Macro [UBERJAR_SERVICES_RESOURCE_TRANSFORMER](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1993) <a name="macro_UBERJAR_SERVICES_RESOURCE_TRANSFORMER"></a>

```ya.make
UBERJAR_SERVICES_RESOURCE_TRANSFORMER()
```

Add ServicesResourceTransformer for UBERJAR() java programs

**See also:** [UBERJAR](#macro_UBERJAR)

### Macro [UDF_NO_PROBE](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L26) <a name="macro_UDF_NO_PROBE"></a>

```ya.make
UDF_NO_PROBE()
```

Disable UDF import check at build stage

### Macro [UDF_NO_SCAN](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L33) <a name="macro_UDF_NO_SCAN"></a>

```ya.make
UDF_NO_SCAN()
```

Disable index of UDF

### Macro [UPDATE_VCS_JAVA_INFO_NODEP](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4203) <a name="macro_UPDATE_VCS_JAVA_INFO_NODEP"></a>

```ya.make
UPDATE_VCS_JAVA_INFO_NODEP(Jar)
```

_Not documented yet._

### Macro [USE_ANNOTATION_PROCESSOR](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L661) <a name="macro_USE_ANNOTATION_PROCESSOR"></a>

```ya.make
USE_ANNOTATION_PROCESSOR(Path)
```

Used to specify annotation processor for building JAVA_PROGRAM() and JAVA_LIBRARY().

### Macro [USE_COMMON_GOOGLE_APIS](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L363) <a name="macro_USE_COMMON_GOOGLE_APIS"></a>

```ya.make
USE_COMMON_GOOGLE_APIS(APIS...)
```

_Not documented yet._

### Macro [USE_CXX](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4421) <a name="macro_USE_CXX"></a>

```ya.make
USE_CXX()
```

Add dependency on C++ runtime
**Note:** This macro is inteneded for use in _GO_BASE_UNIT like module when the module is built without C++ runtime by default

### Macro [USE_DYNAMIC_CUDA](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1346) <a name="macro_USE_DYNAMIC_CUDA"></a>

```ya.make
USE_DYNAMIC_CUDA()
```

Enable linking of PROGRAM with dynamic CUDA. By default CUDA uses static linking

### Macro [USE_ERROR_PRONE](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1847) <a name="macro_USE_ERROR_PRONE"></a>

```ya.make
USE_ERROR_PRONE()
```

Use errorprone instead of javac for .java compilation.

### Macro [USE_JAVALITE](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L537) <a name="macro_USE_JAVALITE"></a>

```ya.make
USE_JAVALITE()
```

Use protobuf-javalite for Java

### Macro [USE_KTLINT_OLD](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2486) <a name="macro_USE_KTLINT_OLD"></a>

```ya.make
USE_KTLINT_OLD()
```

Marks that need use the old version of ktlint

### Macro [USE_LEGACY_PNPM_VIRTUAL_STORE](https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L80) <a name="macro_USE_LEGACY_PNPM_VIRTUAL_STORE"></a>

```ya.make
USE_LEGACY_PNPM_VIRTUAL_STORE()
```

_Not documented yet._

### Macro [USE_LINKER_GOLD](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L883) <a name="macro_USE_LINKER_GOLD"></a>

```ya.make
USE_LINKER_GOLD()
```

Use gold linker for a program. This doesn't work in libraries

### Macro [USE_LLVM_BC16](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4953) <a name="macro_USE_LLVM_BC16"></a>

```ya.make
USE_LLVM_BC16()
```

_Not documented yet._

### Macro [USE_LLVM_BC18](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4958) <a name="macro_USE_LLVM_BC18"></a>

```ya.make
USE_LLVM_BC18()
```

_Not documented yet._

### Macro [USE_LLVM_BC20](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4963) <a name="macro_USE_LLVM_BC20"></a>

```ya.make
USE_LLVM_BC20()
```

_Not documented yet._

### Macro [USE_MODERN_FLEX](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L112) <a name="macro_USE_MODERN_FLEX"></a>

```ya.make
USE_MODERN_FLEX()
```

Use `contrib/tools/flex` as flex tool. Default is `contrib/tools/flex-old`.
**note:** by default no header is emitted. Use `USE_MODERN_FLEX_WITH_HEADER` to add header emission.

### Macro [USE_MODERN_FLEX_WITH_HEADER](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L123) <a name="macro_USE_MODERN_FLEX_WITH_HEADER"></a>

```ya.make
USE_MODERN_FLEX_WITH_HEADER(<header_suffix>)
```

Use `contrib/tools/flex` as flex tool. Default is `contrib/tools/flex-old`.
Additionally emit headers with suffix provided. Header suffix should include extension `.h`.

**example:** USE_MODERN_FLEX_WITH_HEADER(_lexer.h)

### Macro [USE_NASM](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4438) <a name="macro_USE_NASM"></a>

```ya.make
USE_NASM()
```

Build only .asm files with nasm toolchain instead of yasm
Add to ya.make file ADDINCL(asm ...) with all folders where .asm files include smth

### Macro [USE_OLD_FLEX](https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L132) <a name="macro_USE_OLD_FLEX"></a>

```ya.make
USE_OLD_FLEX()
```

Use `contrib/tools/flex-old` as flex tool. This is current default.

### Macro [USE_PERSISTENT_RECIPE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1747) <a name="macro_USE_PERSISTENT_RECIPE"></a>

```ya.make
USE_PERSISTENT_RECIPE(PackagePath)
```

!!!!! EXPERIMENTAL !!!!!
Enables persistent recipe mode for the test suite.
Each USE_PERSISTENT_RECIPE call registers one recipe package.
All registered recipes are started independently and do not see each other's environment.
Each recipe communicates with the test exclusively via its env.json.txt file.

The recipe binary must be named 'recipe_bin' in the PACKAGE module
(via BUNDLE(... NAME recipe_bin)).
The launch command must be declared in a file 'recipe_cmd' (via FILES(recipe_cmd)).

### Macro [USE_PLANTUML](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L275) <a name="macro_USE_PLANTUML"></a>

```ya.make
USE_PLANTUML()
```

Use PlantUML plug-in for yfm builder to render UML diagrams into documentation

### Macro [USE_PYTHON2](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1046) <a name="macro_USE_PYTHON2"></a>

```ya.make
USE_PYTHON2()
```

This adds Python 2.x runtime library to your LIBRARY and makes it Python2-compatible.
Compatibility means proper PEERDIRs, ADDINCLs and variant selection on PEERDIRs to multimodules.

If you'd like to use #include <Python.h> with Python2 specify USE_PYTHON2 or better make it PY2_LIBRARY.
If you'd like to use #include <Python.h> with Python3 specify USE_PYTHON3 or better make it PY3_LIBRARY.
If you'd like to use #include <Python.h> with both Python2 and Python3 convert your LIBRARY to PY23_LIBRARY.

**See also:** [PY2_LIBRARY](#module_PY2_LIBRARY), [PY3_LIBRARY](#module_PY3_LIBRARY), [PY23_LIBRARY](#multimodule_PY23_LIBRARY)

### Macro [USE_PYTHON3](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1063) <a name="macro_USE_PYTHON3"></a>

```ya.make
USE_PYTHON3()
```

This adds Python3 library to your LIBRARY and makes it Python3-compatible.
Compatibility means proper PEERDIRs, ADDINCLs and variant selection on PEERDIRs to multimodules.

If you'd like to use #include <Python.h> with Python3 specify USE_PYTHON3 or better make it PY3_LIBRARY.
If you'd like to use #include <Python.h> with Python2 specify USE_PYTHON2 or better make it PY2_LIBRARY.
If you'd like to use #include <Python.h> with both Python2 and Python3 convert your LIBRARY to PY23_LIBRARY.

**See also:** [PY2_LIBRARY](#module_PY2_LIBRARY), [PY3_LIBRARY](#module_PY3_LIBRARY), [PY23_LIBRARY](#multimodule_PY23_LIBRARY)

### Macro [USE_RECIPE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1730) <a name="macro_USE_RECIPE"></a>

```ya.make
USE_RECIPE(path [arg1 arg2...])
```

Provides prepared environment via recipe for test.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/recipes

### Macro [USE_SA_PLUGINS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L176) <a name="macro_USE_SA_PLUGINS"></a>

```ya.make
USE_SA_PLUGINS(FROM path/to/external/module1 NAME VAR_NAME1 FROM path/to/external/module2 NAME VAR_NAME2 ...)
```

Select additional plugins for clang static analyzer, each path/to/external/module should declare target RESOURCES_LIBRARY.
VAR_NAME should be the same value that was passed into DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE as first argument.
See example in market/report/csa_checks/static_analyzer_ymake.inc

### Macro [USE_SKIFF](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L305) <a name="macro_USE_SKIFF"></a>

```ya.make
USE_SKIFF() #wip, do not use
```

Use mapreduce/yt/skiff_proto/plugin for C++

### Macro [USE_UTIL](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4429) <a name="macro_USE_UTIL"></a>

```ya.make
USE_UTIL()
```

Add dependency on util and C++ runtime
**Note:** This macro is intended for use in _GO_BASE_UNIT like module when the module is build without util by default

### Macro [USRV_GEN_GRPC_CLIENT_V2](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L338) <a name="macro_USRV_GEN_GRPC_CLIENT_V2"></a>

_Not documented yet._

### Macro [USRV_GEN_GRPC_CLIENT_V2_STRUCTS](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L343) <a name="macro_USRV_GEN_GRPC_CLIENT_V2_STRUCTS"></a>

_Not documented yet._

### Macro [USRV_GEN_GRPC_SERVICE_V2](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L348) <a name="macro_USRV_GEN_GRPC_SERVICE_V2"></a>

_Not documented yet._

### Macro [USRV_GEN_GRPC_SERVICE_V2_STRUCTS](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L353) <a name="macro_USRV_GEN_GRPC_SERVICE_V2_STRUCTS"></a>

_Not documented yet._

### Macro [USRV_GEN_PROTO_STRUCTS](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L318) <a name="macro_USRV_GEN_PROTO_STRUCTS"></a>

_Not documented yet._

### Macro [VALIDATE_DATA_RESTART](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2922) <a name="macro_VALIDATE_DATA_RESTART"></a>

```ya.make
VALIDATE_DATA_RESTART(ext)
```

Change uid for resource validation tests. May be useful when sandbox resource ttl is changed, but test status is cached in CI.
You can change ext to change test's uid. For example VALIDATE_DATA_RESTART(X), where is X is current revision.

### Macro [VALIDATE_IN_DIRS](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L38) <a name="macro_VALIDATE_IN_DIRS"></a>

_Not documented yet._

### Macro [VCS_INFO_FILE](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4217) <a name="macro_VCS_INFO_FILE"></a>

```ya.make
VCS_INFO_FILE([FILE out_file])
```

Enable saving vcs info as a json-file into PACKAGE

Info is saved to 'vcs_info.json' by default.
Use FILE parameter if you want another name.

**Note:** macro can be used only once per module

### Macro [VERSION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4606) <a name="macro_VERSION"></a>

```ya.make
VERSION(Args...)
```

Specify version of a module. Currently unused by build system, only informative.

### Macro [VISIBILITY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5626) <a name="macro_VISIBILITY"></a>

```ya.make
VISIBILITY(level)
```

This macro sets visibility level for symbols compiled for the current module. 'level'
may take only one of the following values: DEFAULT, HIDDEN.

### Macro [VITE_OUTPUT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L30) <a name="macro_VITE_OUTPUT"></a>

```ya.make
VITE_OUTPUT(DirName)
```

_Not documented yet._

### Macro [WEBPACK_OUTPUT](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L28) <a name="macro_WEBPACK_OUTPUT"></a>

```ya.make
WEBPACK_OUTPUT(FirstDirName, DirNames...)
```

_Not documented yet._

### Macro [WINDOWS_LONG_PATH_MANIFEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5611) <a name="macro_WINDOWS_LONG_PATH_MANIFEST"></a>

```ya.make
WINDOWS_LONG_PATH_MANIFEST()
```

_Not documented yet._

### Macro [WINDOWS_MANIFEST](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5606) <a name="macro_WINDOWS_MANIFEST"></a>

```ya.make
WINDOWS_MANIFEST(Manifest)
```

_Not documented yet._

### Macro [WITHOUT_LICENSE_TEXTS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5700) <a name="macro_WITHOUT_LICENSE_TEXTS"></a>

```ya.make
WITHOUT_LICENSE_TEXTS()
```

This macro indicates that the module has no license text

### Macro [WITHOUT_VERSION](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5802) <a name="macro_WITHOUT_VERSION"></a>

```ya.make
WITHOUT_VERSION()
```

_Not documented yet._

### Macro [WITH_DYNAMIC_LIBS](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1076) <a name="macro_WITH_DYNAMIC_LIBS"></a>

```ya.make
WITH_DYNAMIC_LIBS() # restricted
```

Include dynamic libraries as extra PROGRAM/DLL outputs

### Macro [WITH_JDK](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2188) <a name="macro_WITH_JDK"></a>

```ya.make
WITH_JDK()
```

Add directory with JDK to JAVA_PROGRAM output

### Macro [WITH_KAPT](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2219) <a name="macro_WITH_KAPT"></a>

```ya.make
WITH_KAPT()
```

Use kapt for as annotation processor

### Macro [WITH_KOTLIN](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2207) <a name="macro_WITH_KOTLIN"></a>

```ya.make
WITH_KOTLIN()
```

Compile kotlin source code in this java module

### Macro [WITH_KOTLINC_ALLOPEN](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2251) <a name="macro_WITH_KOTLINC_ALLOPEN"></a>

```ya.make
WITH_KOTLINC_ALLOPEN(-flags)
```

Enable allopen kotlin compiler plugin https://kotlinlang.org/docs/all-open-plugin.html

### Macro [WITH_KOTLINC_DETEKT](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2297) <a name="macro_WITH_KOTLINC_DETEKT"></a>

```ya.make
WITH_KOTLINC_DETEKT(-flags)
```

Enable detekt kotlin compiler plugin https://detekt.dev/docs/gettingstarted/compilerplugin/

### Macro [WITH_KOTLINC_LOMBOK](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2262) <a name="macro_WITH_KOTLINC_LOMBOK"></a>

```ya.make
WITH_KOTLINC_LOMBOK(-flags)
```

Enable lombok kotlin compiler plugin https://kotlinlang.org/docs/lombok.html

### Macro [WITH_KOTLINC_NOARG](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2274) <a name="macro_WITH_KOTLINC_NOARG"></a>

```ya.make
WITH_KOTLINC_NOARG(-flags)
```

Enable noarg kotlin compiler plugin https://kotlinlang.org/docs/no-arg-plugin.html

### Macro [WITH_KOTLINC_SERIALIZATION](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2286) <a name="macro_WITH_KOTLINC_SERIALIZATION"></a>

```ya.make
WITH_KOTLINC_SERIALIZATION()
```

Enable serialization kotlin compiler plugin https://kotlinlang.org/docs/serialization.html

### Macro [WITH_KOTLIN_GRPC](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L231) <a name="macro_WITH_KOTLIN_GRPC"></a>

```ya.make
WITH_KOTLIN_GRPC()
```

_Not documented yet._

### Macro [WITH_NODE_MODULES](https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L27) <a name="macro_WITH_NODE_MODULES"></a>

```ya.make
WITH_NODE_MODULES()
```

Macro configures the project to output node_modules bundle as a build-result.

**Documentation:** https://docs.yandex-team.ru/frontend-in-arcadia/references/macros#with-node-modules

### Macro [WITH_YA_1931](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2514) <a name="macro_WITH_YA_1931"></a>

```ya.make
WITH_YA_1931()
```

Interim macro to temporarily remove ALL_SRCDIRS from being added to ktlint test sources.

### Macro [YABS_GENERATE_CONF](https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L11) <a name="macro_YABS_GENERATE_CONF"></a>

_Not documented yet._

### Macro [YABS_GENERATE_PHANTOM_CONF_PATCH](https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L43) <a name="macro_YABS_GENERATE_PHANTOM_CONF_PATCH"></a>

_Not documented yet._

### Macro [YABS_GENERATE_PHANTOM_CONF_TEST_CHECK](https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L54) <a name="macro_YABS_GENERATE_PHANTOM_CONF_TEST_CHECK"></a>

_Not documented yet._

### Macro [YA_CONF_JSON](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5721) <a name="macro_YA_CONF_JSON"></a>

Add passed ya.conf.json and all bottle's formula external files to resources
File MUST be arcadia root relative path (without "${ARCADIA_ROOT}/" prefix).
**NOTE:**
  An external formula file referenced from ya.conf.json must be passed as an arcadia root relative path and
  should be located in any subdirectory of the ya.conf.json location ("build/" if we consider a production).
  The later restriction prevents problems in selectively checkouted arcadia.

### Macro [YDL_DESC_USE_BINARY](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3770) <a name="macro_YDL_DESC_USE_BINARY"></a>

```ya.make
YDL_DESC_USE_BINARY()
```

Used in conjunction with BUILD_YDL_DESC. When enabled, all generated descriptors are binary.

**example:**

    PACKAGE()
        YDL_DESC_USE_BINARY()
        BUILD_YDL_DESC(../types.ydl Event Event.ydld)
    END()

This will generate descriptor Event.ydld in a binary format.

### Macro [YQL_ABI_VERSION](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L249) <a name="macro_YQL_ABI_VERSION"></a>

```ya.make
YQL_ABI_VERSION(major minor release))
```

Specifying the supported ABI for YQL_UDF.

**See also:** [YQL_UDF()](#multimodule_YQL_UDF)

### Macro [YQL_LAST_ABI_VERSION](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L258) <a name="macro_YQL_LAST_ABI_VERSION"></a>

```ya.make
YQL_LAST_ABI_VERSION()
```

Use the last ABI for YQL_UDF

### Macro [YT_ORM_PROTO_YSON](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L446) <a name="macro_YT_ORM_PROTO_YSON"></a>

```ya.make
YT_ORM_PROTO_YSON(Files... OUT_OPTS Opts...)
```

Generate .yson.go from .proto using yt/yt/orm/go/codegen/yson/internal/proto-yson-gen/cmd/proto-yson-gen

### Macro [YT_RECORD_DISABLE_PEERDIR](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yt.conf?rev=20020720#L3) <a name="macro_YT_RECORD_DISABLE_PEERDIR"></a>

```ya.make
YT_RECORD_DISABLE_PEERDIR()
```

_Not documented yet._

### Macro [YT_SPEC](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1587) <a name="macro_YT_SPEC"></a>

```ya.make
YT_SPEC(path1 [path2...])
```

Allows you to specify json-files with YT task and operation specs,
which will be used to run test node in the YT.
Test must be marked with ya:yt tag.
Files must be relative to the root of Arcadia.

**Documentation:** https://wiki.yandex-team.ru/yatool/test/

## Properties <a name="properties"></a>

### Property [ALIASES](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L31) <a name="property_ALIASES"></a>

Defines macro name aliases for the module. When a macro FROM is used, it is redirected to macro TO before processing.

### Property [ALLOWED](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L34) <a name="property_ALLOWED"></a>

Restricts macros list allowed within the module.

### Property [ALLOWED_IN_LINTERS_MAKE](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L35) <a name="property_ALLOWED_IN_LINTERS_MAKE"></a>

_Not documented yet._

### Property [ARGS_PARSER](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L43) <a name="property_ARGS_PARSER"></a>

Choose argument parser for macro opening curent module declaration. Must be one of: `Base`, `DLL` or `Raw`

 * `Base` - Effective signature: `(Realprjname, PREFIX=)`. Value of the only positional parameter is stored in the REALPRJNAME variable.
            Value of the optional named parameter `PREFIX` is used to set MODULE_PREFIX variable.
            **Default** arg parser for module macros.
 * `DLL` - Effective signature: `(Realprjname, PREFIX=, Ver...)`. First positional parameter and the only named parameter PREFIX are treated in the same way as in Base
           argument parser. Remaining positional parameters are treated as components of DLL so-version and are stored in a `MODULE_VERSION` variable in a joined by `.` string
 * `Raw` - Do not perform any parsing or validation. Stores all arguments in a variable `MODULE_ARGS_RAW` which can be analyzed by macros invoked in the module body.

### Property [CMD](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L44) <a name="property_CMD"></a>

Macro or module build command

### Property [DEFAULT_NAME_GENERATOR](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L45) <a name="property_DEFAULT_NAME_GENERATOR"></a>

Name of embedded output filename generator, one of: UseDirNameOrSetGoPackage, TwoDirNames, ThreeDirNames, FullPath

### Property [EPILOGUE](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L46) <a name="property_EPILOGUE"></a>

Name of a macro to invoke after the module body is fully parsed.

### Property [EXTS](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L47) <a name="property_EXTS"></a>

_Not documented yet._

### Property [FILE_GROUP](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L91) <a name="property_FILE_GROUP"></a>

__EXPERIMENTAL FEATUE__ allows to create complex group of files with graph representation similar to GLOB or ALL_SRCS. Not yet ready for production.

### Property [FINAL_TARGET](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L50) <a name="property_FINAL_TARGET"></a>

Marks the module as a final build target.

### Property [GEN_FROM_FILE](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L53) <a name="property_GEN_FROM_FILE"></a>

Mark command as embedding configuration variables into files. Adds configuration variables in form of key=value to the end of .CMD.

### Property [GLOBAL](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L56) <a name="property_GLOBAL"></a>

Makes listed variables global. For each listed name a corresponding NAME_GLOBAL variable is created to collect values across dependent modules.

### Property [GLOBAL_CMD](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L57) <a name="property_GLOBAL_CMD"></a>

Build command for global sources (e.g. SRCS(GLOBAL ...)). Must be accompanied by .GLOBAL_EXTS.

### Property [GLOBAL_EXTS](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L60) <a name="property_GLOBAL_EXTS"></a>

Specify extensions which are treated as global inputs and processed by .GLOBAL_CMD.

### Property [GLOBAL_SEM](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L61) <a name="property_GLOBAL_SEM"></a>

Global semantics (instead of global commands) for export to other build systems in --sem-graph mode

### Property [IGNORED](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L64) <a name="property_IGNORED"></a>

Lists macros that are silently ignored within the module (neither processed nor causing an error).

### Property [INCLUDE_TAG](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L67) <a name="property_INCLUDE_TAG"></a>

Controls whether a multimodule sub-module tag is included in the default set of active tags.

### Property [NODE_TYPE](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L70) <a name="property_NODE_TYPE"></a>

Required. Sets the module node type in the build graph.

### Property [NO_EXPAND](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L73) <a name="property_NO_EXPAND"></a>

Prevents expansion of the macro command variables during command evaluation.

### Property [PEERDIRSELF](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L80) <a name="property_PEERDIRSELF"></a>

Declares intra-multimodule dependencies: lists sub-module tags that the current sub-module depends on within the same multimodule.

### Property [PEERDIR_POLICY](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L77) <a name="property_PEERDIR_POLICY"></a>

Controls how PEERDIRs to the module work. as_build_from makes dependants to just use results produced by the module; as_include makes dependants to include the module as a whole (with transitive info, for example).

### Property [PROXY](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L81) <a name="property_PROXY"></a>

_Not documented yet._

### Property [RESTRICTED](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L87) <a name="property_RESTRICTED"></a>

Restricts listed macros from being used within the module. Complementary to .ALLOWED and .IGNORED properties.

### Property [SEM](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L88) <a name="property_SEM"></a>

Semantics (instead of commands) for export to other build systems in --sem-graph mode

### Property [SYMLINK_POLICY](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L89) <a name="property_SYMLINK_POLICY"></a>

_Not documented yet._

### Property [TRANSITION](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L94) <a name="property_TRANSITION"></a>

Marks the module to be configured in foreign platform. Supported platforms now are pic, nopic.

### Property [USE_PEERS_LATE_OUTS](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L90) <a name="property_USE_PEERS_LATE_OUTS"></a>

_Not documented yet._

### Property [VERSION_PROXY](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L84) <a name="property_VERSION_PROXY"></a>

Such module is always replaced by exact version of the library in dependency management phase of build configuration. It can only be used with dependency management aware modules.

## Variables <a name="variables"></a>

### Variable [APPLIED_EXCLUDES](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L23) <a name="variable_APPLIED_EXCLUDES"></a>

_Not documented yet._

### Variable [ARCADIA_BUILD_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L24) <a name="variable_ARCADIA_BUILD_ROOT"></a>

build output root directory

### Variable [ARCADIA_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L25) <a name="variable_ARCADIA_ROOT"></a>

source files root directory

### Variable [AUTO_INPUT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L26) <a name="variable_AUTO_INPUT"></a>

_Not documented yet._

### Variable [BINDIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L27) <a name="variable_BINDIR"></a>

module directory within a build tree, ARCADIA_BUILD_ROOT / MODDIR

### Variable [CHECK_INTERNAL](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L28) <a name="variable_CHECK_INTERNAL"></a>

_Not documented yet._

### Variable [CMAKE_CURRENT_BINARY_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L29) <a name="variable_CMAKE_CURRENT_BINARY_DIR"></a>

_Not documented yet._

### Variable [CMAKE_CURRENT_SOURCE_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L30) <a name="variable_CMAKE_CURRENT_SOURCE_DIR"></a>

_Not documented yet._

### Variable [CONSUME_NON_MANAGEABLE_PEERS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L31) <a name="variable_CONSUME_NON_MANAGEABLE_PEERS"></a>

_Not documented yet._

### Variable [CURDIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L32) <a name="variable_CURDIR"></a>

module directory within a source tree, ARCADIA_ROOT / MODDIR

### Variable [DART_CLASSPATH](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L34) <a name="variable_DART_CLASSPATH"></a>

_Not documented yet._

### Variable [DART_CLASSPATH_DEPS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L33) <a name="variable_DART_CLASSPATH_DEPS"></a>

_Not documented yet._

### Variable [DEFAULT_MODULE_LICENSE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L35) <a name="variable_DEFAULT_MODULE_LICENSE"></a>

Default license for modules that do not set the LICENSE explicitly

### Variable [DEPENDENCY_MANAGEMENT_TAGS_EXCLUDE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L37) <a name="variable_DEPENDENCY_MANAGEMENT_TAGS_EXCLUDE"></a>

Module tags to exclude from managed peers closure for current module but propagate further

### Variable [DEPENDENCY_MANAGEMENT_TRANSPARENT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L38) <a name="variable_DEPENDENCY_MANAGEMENT_TRANSPARENT"></a>

If yes: module does not apply local DEPENDENCY_MANAGEMENT/EXCLUDE rules; its own MANAGED_PEERS_CLOSURE lists only direct PEERDIR module nodes. Upstream closure still expands through those directs' full managed closures. Requires HAS_MANAGEABLE_PEERS=yes

### Variable [DEPENDENCY_MANAGEMENT_VALUE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L36) <a name="variable_DEPENDENCY_MANAGEMENT_VALUE"></a>

_Not documented yet._

### Variable [DONT_RESOLVE_INCLUDES](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L39) <a name="variable_DONT_RESOLVE_INCLUDES"></a>

_Not documented yet._

### Variable [DYNAMIC_LINK](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L40) <a name="variable_DYNAMIC_LINK"></a>

_Not documented yet._

### Variable [EV_HEADER_EXTS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L41) <a name="variable_EV_HEADER_EXTS"></a>

_Not documented yet._

### Variable [EXCLUDE_SUBMODULES](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L42) <a name="variable_EXCLUDE_SUBMODULES"></a>

_Not documented yet._

### Variable [EXCLUDE_VALUE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L43) <a name="variable_EXCLUDE_VALUE"></a>

_Not documented yet._

### Variable [EXPORTED_BUILD_SYSTEM_BUILD_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L44) <a name="variable_EXPORTED_BUILD_SYSTEM_BUILD_ROOT"></a>

_Not documented yet._

### Variable [EXPORTED_BUILD_SYSTEM_SOURCE_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L45) <a name="variable_EXPORTED_BUILD_SYSTEM_SOURCE_ROOT"></a>

_Not documented yet._

### Variable [GLOBAL_SUFFIX](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L47) <a name="variable_GLOBAL_SUFFIX"></a>

_Not documented yet._

### Variable [GLOBAL_TARGET](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L48) <a name="variable_GLOBAL_TARGET"></a>

_Not documented yet._

### Variable [GO_HAS_INTERNAL_TESTS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L49) <a name="variable_GO_HAS_INTERNAL_TESTS"></a>

_Not documented yet._

### Variable [GO_TEST_FOR_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L50) <a name="variable_GO_TEST_FOR_DIR"></a>

_Not documented yet._

### Variable [HAS_MANAGEABLE_PEERS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L51) <a name="variable_HAS_MANAGEABLE_PEERS"></a>

_Not documented yet._

### Variable [IGNORE_JAVA_DEPENDENCIES_CONFIGURATION](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L52) <a name="variable_IGNORE_JAVA_DEPENDENCIES_CONFIGURATION"></a>

_Not documented yet._

### Variable [INPUT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L53) <a name="variable_INPUT"></a>

_Not documented yet._

### Variable [INTERNAL_EXCEPTIONS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L54) <a name="variable_INTERNAL_EXCEPTIONS"></a>

_Not documented yet._

### Variable [JAVA_DEPENDENCIES_CONFIGURATION_VALUE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L55) <a name="variable_JAVA_DEPENDENCIES_CONFIGURATION_VALUE"></a>

_Not documented yet._

### Variable [MANAGED_PEERS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L57) <a name="variable_MANAGED_PEERS"></a>

_Not documented yet._

### Variable [MANAGED_PEERS_CLOSURE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L56) <a name="variable_MANAGED_PEERS_CLOSURE"></a>

_Not documented yet._

### Variable [MANGLED_MODULE_TYPE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L58) <a name="variable_MANGLED_MODULE_TYPE"></a>

_Not documented yet._

### Variable [MODDIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L59) <a name="variable_MODDIR"></a>

module directory w/o specifying a root

### Variable [MODULE_ARGS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L60) <a name="variable_MODULE_ARGS"></a>

_Not documented yet._

### Variable [MODULE_COMMON_CONFIGS_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L61) <a name="variable_MODULE_COMMON_CONFIGS_DIR"></a>

_Not documented yet._

### Variable [MODULE_KIND](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L62) <a name="variable_MODULE_KIND"></a>

_Not documented yet._

### Variable [MODULE_LANG](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L63) <a name="variable_MODULE_LANG"></a>

_Not documented yet._

### Variable [MODULE_PREFIX](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L64) <a name="variable_MODULE_PREFIX"></a>

_Not documented yet._

### Variable [MODULE_SEM_IGNORE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L70) <a name="variable_MODULE_SEM_IGNORE"></a>

Skip traverse into module during render sem-graph, add IGNORED to semantics

### Variable [MODULE_SUFFIX](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L65) <a name="variable_MODULE_SUFFIX"></a>

_Not documented yet._

### Variable [MODULE_TYPE](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L69) <a name="variable_MODULE_TYPE"></a>

_Not documented yet._

### Variable [NON_NAMAGEABLE_PEERS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L72) <a name="variable_NON_NAMAGEABLE_PEERS"></a>

_Not documented yet._

### Variable [OUTPUT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L73) <a name="variable_OUTPUT"></a>

_Not documented yet._

### Variable [PASS_PEERS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L76) <a name="variable_PASS_PEERS"></a>

If set, module peers are passed to it's dependendants.

### Variable [PEERDIR_TAGS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L77) <a name="variable_PEERDIR_TAGS"></a>

_Not documented yet._

### Variable [PEERS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L79) <a name="variable_PEERS"></a>

a list of module dependencies for the module

### Variable [PEERS_LATE_OUTS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L78) <a name="variable_PEERS_LATE_OUTS"></a>

_Not documented yet._

### Variable [PROTO_HEADER_EXTS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L80) <a name="variable_PROTO_HEADER_EXTS"></a>

_Not documented yet._

### Variable [PYTHON_BIN](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L81) <a name="variable_PYTHON_BIN"></a>

_Not documented yet._

### Variable [REALPRJNAME](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L82) <a name="variable_REALPRJNAME"></a>

_Not documented yet._

### Variable [SONAME](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L83) <a name="variable_SONAME"></a>

_Not documented yet._

### Variable [SRCS_GLOBAL](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L85) <a name="variable_SRCS_GLOBAL"></a>

_Not documented yet._

### Variable [START_TARGET](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L86) <a name="variable_START_TARGET"></a>

_Not documented yet._

### Variable [TARGET](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L87) <a name="variable_TARGET"></a>

_Not documented yet._

### Variable [TEST_CASE_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L88) <a name="variable_TEST_CASE_ROOT"></a>

_Not documented yet._

### Variable [TEST_OUT_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L89) <a name="variable_TEST_OUT_ROOT"></a>

_Not documented yet._

### Variable [TEST_SOURCE_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L90) <a name="variable_TEST_SOURCE_ROOT"></a>

_Not documented yet._

### Variable [TEST_WORK_ROOT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L91) <a name="variable_TEST_WORK_ROOT"></a>

_Not documented yet._

### Variable [TOOLS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L92) <a name="variable_TOOLS"></a>

_Not documented yet._

### Variable [TS_CONFIG_DECLARATION](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L94) <a name="variable_TS_CONFIG_DECLARATION"></a>

_Not documented yet._

### Variable [TS_CONFIG_DECLARATION_MAP](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L93) <a name="variable_TS_CONFIG_DECLARATION_MAP"></a>

_Not documented yet._

### Variable [TS_CONFIG_DEDUCE_OUT](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L95) <a name="variable_TS_CONFIG_DEDUCE_OUT"></a>

_Not documented yet._

### Variable [TS_CONFIG_OUT_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L96) <a name="variable_TS_CONFIG_OUT_DIR"></a>

_Not documented yet._

### Variable [TS_CONFIG_PRESERVE_JSX](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L97) <a name="variable_TS_CONFIG_PRESERVE_JSX"></a>

_Not documented yet._

### Variable [TS_CONFIG_ROOT_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L98) <a name="variable_TS_CONFIG_ROOT_DIR"></a>

_Not documented yet._

### Variable [TS_CONFIG_SOURCE_MAP](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L99) <a name="variable_TS_CONFIG_SOURCE_MAP"></a>

_Not documented yet._

### Variable [UNITTEST_DIR](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L100) <a name="variable_UNITTEST_DIR"></a>

_Not documented yet._

### Variable [UNITTEST_MOD](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L101) <a name="variable_UNITTEST_MOD"></a>

_Not documented yet._

### Variable [USE_ALL_SRCS](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L102) <a name="variable_USE_ALL_SRCS"></a>

_Not documented yet._

### Variable [USE_GLOBAL_CMD](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L103) <a name="variable_USE_GLOBAL_CMD"></a>

_Not documented yet._

## Undocumented <a name="undocumented"></a>

<details><summary>218 entities without a description comment in the source. Patches welcome.</summary>

| Name | Type | Source |
| --- | --- | --- |
| [`JAVA_CONTRIB_ANNOTATION_PROCESSOR`](#multimodule_JAVA_CONTRIB_ANNOTATION_PROCESSOR) | Multimodule | [build/conf/java.conf:154](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L154) |
| [`JAVA_CONTRIB_PROGRAM`](#multimodule_JAVA_CONTRIB_PROGRAM) | Multimodule | [build/conf/java.conf:418](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L418) |
| [`JTEST`](#multimodule_JTEST) | Multimodule | [build/conf/java.conf:317](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L317) |
| [`JTEST_FOR`](#multimodule_JTEST_FOR) | Multimodule | [build/conf/java.conf:377](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L377) |
| [`JUNIT5`](#multimodule_JUNIT5) | Multimodule | [build/conf/java.conf:255](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L255) |
| [`JUNIT6`](#multimodule_JUNIT6) | Multimodule | [build/conf/java.conf:191](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L191) |
| [`PY23_TEST`](#multimodule_PY23_TEST) | Multimodule | [build/conf/python.conf:1263](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1263) |
| [`TS_NEXT`](#multimodule_TS_NEXT) | Multimodule | [build/conf/ts/ts_next.conf:74](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L74) |
| [`TS_RSPACK`](#multimodule_TS_RSPACK) | Multimodule | [build/conf/ts/ts_rspack.conf:49](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L49) |
| [`TS_TEST_FOR`](#multimodule_TS_TEST_FOR) | Multimodule | [build/conf/ts/ts_check.conf:16](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L16) |
| [`TS_TSC`](#multimodule_TS_TSC) | Multimodule | [build/conf/ts/ts_tsc.conf:22](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_tsc.conf?rev=20020720#L22) |
| [`TS_VITE`](#multimodule_TS_VITE) | Multimodule | [build/conf/ts/ts_vite.conf:58](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L58) |
| [`TS_WEBPACK`](#multimodule_TS_WEBPACK) | Multimodule | [build/conf/ts/ts_webpack.conf:56](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L56) |
| [`YQL_UDF_CONTRIB`](#multimodule_YQL_UDF_CONTRIB) | Multimodule | [build/conf/project_specific/yql_udf.conf:225](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L225) |
| [`YQL_UDF_YDB`](#multimodule_YQL_UDF_YDB) | Multimodule | [build/conf/project_specific/yql_udf.conf:204](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L204) |
| [`DEFAULT_IOS_INTERFACE`](#module_DEFAULT_IOS_INTERFACE) | Module | [build/ymake.core.conf:5544](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5544) |
| [`DOCS_LIBRARY`](#module_DOCS_LIBRARY) | Module | [build/conf/docs.conf:88](https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L88) |
| [`JAVA_CONTRIB`](#module_JAVA_CONTRIB) | Module | [build/conf/java.conf:765](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L765) |
| [`JAVA_CONTRIB_PROXY`](#module_JAVA_CONTRIB_PROXY) | Module | [build/conf/java.conf:709](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L709) |
| [`JAVA_TEST_LIBRARY`](#module_JAVA_TEST_LIBRARY) | Module | [build/conf/java.conf:44](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L44) |
| [`PROTO_DESCRIPTIONS`](#module_PROTO_DESCRIPTIONS) | Module | [build/conf/proto.conf:976](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L976) |
| [`PROTO_REGISTRY`](#module_PROTO_REGISTRY) | Module | [build/conf/proto.conf:989](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L989) |
| [`TS_TEST_HERMIONE_FOR`](#module_TS_TEST_HERMIONE_FOR) | Module | [build/conf/ts/ts_test.conf:95](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L95) |
| [`TS_TEST_JEST_FOR`](#module_TS_TEST_JEST_FOR) | Module | [build/conf/ts/ts_test.conf:30](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L30) |
| [`TS_TEST_PLAYWRIGHT_FOR`](#module_TS_TEST_PLAYWRIGHT_FOR) | Module | [build/conf/ts/ts_test.conf:129](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L129) |
| [`TS_TEST_PLAYWRIGHT_LARGE_FOR`](#module_TS_TEST_PLAYWRIGHT_LARGE_FOR) | Module | [build/conf/ts/ts_test.conf:160](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L160) |
| [`TS_TEST_VITEST_FOR`](#module_TS_TEST_VITEST_FOR) | Module | [build/conf/ts/ts_test.conf:63](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L63) |
| [`YQL_UDF_MODULE_CONTRIB`](#module_YQL_UDF_MODULE_CONTRIB) | Module | [build/conf/project_specific/yql_udf.conf:162](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L162) |
| [`YQL_UDF_YDB_MODULE`](#module_YQL_UDF_YDB_MODULE) | Module | [build/conf/project_specific/yql_udf.conf:156](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L156) |
| [`ACCELEO`](#macro_ACCELEO) | Macro | [build/conf/java.conf:5](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L5) |
| [`ADD_CHECK`](#macro_ADD_CHECK) | Macro | [build/plugins/ytest.py:772](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L772) |
| [`ADD_CHECK_PY_IMPORTS`](#macro_ADD_CHECK_PY_IMPORTS) | Macro | [build/plugins/_dart_fields.py:61](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) |
| [`ADD_CLANG_TIDY`](#macro_ADD_CLANG_TIDY) | Macro | [build/ymake.core.conf:1185](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1185) |
| [`ADD_DLLS_TO_JAR`](#macro_ADD_DLLS_TO_JAR) | Macro | [build/conf/java.conf:2149](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2149) |
| [`ADD_IWYU`](#macro_ADD_IWYU) | Macro | [build/ymake.core.conf:1197](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1197) |
| [`ADD_PYTEST_BIN`](#macro_ADD_PYTEST_BIN) | Macro | [build/plugins/_dart_fields.py:61](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) |
| [`ADD_YTEST`](#macro_ADD_YTEST) | Macro | [build/plugins/ytest.py:1567](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L1567) |
| [`ALLOCATOR_IMPL`](#macro_ALLOCATOR_IMPL) | Macro | [build/conf/opensource.conf:113](https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L113) |
| [`ASSERT`](#macro_ASSERT) | Macro | [build/plugins/macros_with_error.py:30](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L30) |
| [`AUTO_SERVICE`](#macro_AUTO_SERVICE) | Macro | [build/conf/java.conf:122](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L122) |
| [`CHECK_ALLOWED_PATH`](#macro_CHECK_ALLOWED_PATH) | Macro | [build/internal/plugins/container_layers.py:5](https://a.yandex-team.ru/arcadia/build/internal/plugins/container_layers.py?rev=20020720#L5) |
| [`CHECK_CONTRIB_CREDITS`](#macro_CHECK_CONTRIB_CREDITS) | Macro | [build/plugins/credits.py:11](https://a.yandex-team.ru/arcadia/build/plugins/credits.py?rev=20020720#L11) |
| [`CLANG_WARNINGS`](#macro_CLANG_WARNINGS) | Macro | [build/ymake.core.conf:5754](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5754) |
| [`CLEAN_TEXTREL`](#macro_CLEAN_TEXTREL) | Macro | [build/ymake.core.conf:2197](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2197) |
| [`COLLECT_KOTLIN_LINT_SRCS`](#macro_COLLECT_KOTLIN_LINT_SRCS) | Macro | [build/conf/custom_lint.conf:43](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L43) |
| [`COPY`](#macro_COPY) | Macro | [build/plugins/cp.py:6](https://a.yandex-team.ru/arcadia/build/plugins/cp.py?rev=20020720#L6) |
| [`COW`](#macro_COW) | Macro | [build/ymake.core.conf:896](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L896) |
| [`CPP_ADDINCL`](#macro_CPP_ADDINCL) | Macro | [build/ymake.core.conf:5332](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5332) |
| [`CPP_ENUMS_SERIALIZATION`](#macro_CPP_ENUMS_SERIALIZATION) | Macro | [build/plugins/pybuild.py:824](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L824) |
| [`CREATE_INIT_PY_STRUCTURE`](#macro_CREATE_INIT_PY_STRUCTURE) | Macro | [build/plugins/create_init_py.py:6](https://a.yandex-team.ru/arcadia/build/plugins/create_init_py.py?rev=20020720#L6) |
| [`CREDITS_DISCLAIMER`](#macro_CREDITS_DISCLAIMER) | Macro | [build/plugins/credits.py:5](https://a.yandex-team.ru/arcadia/build/plugins/credits.py?rev=20020720#L5) |
| [`DARWIN_SIGNED_RESOURCE`](#macro_DARWIN_SIGNED_RESOURCE) | Macro | [build/ymake.core.conf:5524](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5524) |
| [`DARWIN_STRINGS_RESOURCE`](#macro_DARWIN_STRINGS_RESOURCE) | Macro | [build/ymake.core.conf:5520](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5520) |
| [`DISABLE_DATA_VALIDATION`](#macro_DISABLE_DATA_VALIDATION) | Macro | [build/ymake.core.conf:1602](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1602) |
| [`DOCKER_IMAGE`](#macro_DOCKER_IMAGE) | Macro | [build/ymake.core.conf:1634](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1634) |
| [`EVLOG_CMD`](#macro_EVLOG_CMD) | Macro | [build/conf/proto.conf:1072](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1072) |
| [`EXPLICIT_DATA`](#macro_EXPLICIT_DATA) | Macro | [build/ymake.core.conf:1654](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1654) |
| [`FBS_CMD`](#macro_FBS_CMD) | Macro | [build/conf/fbs.conf:153](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L153) |
| [`FBS_NAMESPACE`](#macro_FBS_NAMESPACE) | Macro | [build/conf/fbs.conf:95](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L95) |
| [`FBS_TO_PY2SRC`](#macro_FBS_TO_PY2SRC) | Macro | [build/conf/fbs.conf:28](https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L28) |
| [`FILES`](#macro_FILES) | Macro | [build/plugins/files.py:4](https://a.yandex-team.ru/arcadia/build/plugins/files.py?rev=20020720#L4) |
| [`GENERATE_SCRIPT`](#macro_GENERATE_SCRIPT) | Macro | [build/plugins/java.py:297](https://a.yandex-team.ru/arcadia/build/plugins/java.py?rev=20020720#L297) |
| [`GENERATE_YT_RECORD`](#macro_GENERATE_YT_RECORD) | Macro | [build/conf/project_specific/yt.conf:7](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yt.conf?rev=20020720#L7) |
| [`GOLANG_VERSION`](#macro_GOLANG_VERSION) | Macro | [build/conf/go.conf:187](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L187) |
| [`GO_MOCKGEN_TYPES`](#macro_GO_MOCKGEN_TYPES) | Macro | [build/conf/go.conf:1213](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1213) |
| [`GO_PROTO_USE_V2`](#macro_GO_PROTO_USE_V2) | Macro | [build/conf/proto.conf:648](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L648) |
| [`GO_SSO`](#macro_GO_SSO) | Macro | [build/conf/go.conf:219](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L219) |
| [`GO_SSO_TOOL`](#macro_GO_SSO_TOOL) | Macro | [build/conf/go.conf:232](https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L232) |
| [`INJECT_PEERS`](#macro_INJECT_PEERS) | Macro | [build/conf/ts/node_modules.conf:84](https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L84) |
| [`IOS_APP_ASSETS_FLAGS`](#macro_IOS_APP_ASSETS_FLAGS) | Macro | [build/ymake.core.conf:5516](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5516) |
| [`IOS_APP_COMMON_FLAGS`](#macro_IOS_APP_COMMON_FLAGS) | Macro | [build/ymake.core.conf:5510](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5510) |
| [`IOS_APP_SETTINGS`](#macro_IOS_APP_SETTINGS) | Macro | [build/plugins/ios_app_settings.py:5](https://a.yandex-team.ru/arcadia/build/plugins/ios_app_settings.py?rev=20020720#L5) |
| [`IOS_ASSETS`](#macro_IOS_ASSETS) | Macro | [build/plugins/ios_assets.py:6](https://a.yandex-team.ru/arcadia/build/plugins/ios_assets.py?rev=20020720#L6) |
| [`JAR_ANNOTATION_PROCESSOR`](#macro_JAR_ANNOTATION_PROCESSOR) | Macro | [build/conf/java.conf:640](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L640) |
| [`JAR_MAIN_CLASS`](#macro_JAR_MAIN_CLASS) | Macro | [build/conf/java.conf:1146](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1146) |
| [`JAR_RESOURCE`](#macro_JAR_RESOURCE) | Macro | [build/conf/java.conf:734](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L734) |
| [`JAVA_MODULE`](#macro_JAVA_MODULE) | Macro | [build/plugins/java.py:41](https://a.yandex-team.ru/arcadia/build/plugins/java.py?rev=20020720#L41) |
| [`JAVA_RESOURCE`](#macro_JAVA_RESOURCE) | Macro | [build/conf/java.conf:1040](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1040) |
| [`JAVA_TEST`](#macro_JAVA_TEST) | Macro | [build/plugins/_dart_fields.py:61](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) |
| [`JAVA_TEST_DEPS`](#macro_JAVA_TEST_DEPS) | Macro | [build/plugins/_dart_fields.py:61](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) |
| [`JNI_EXPORTS`](#macro_JNI_EXPORTS) | Macro | [build/ymake.core.conf:1324](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1324) |
| [`LLVM_BC`](#macro_LLVM_BC) | Macro | [build/plugins/llvm_bc.py:5](https://a.yandex-team.ru/arcadia/build/plugins/llvm_bc.py?rev=20020720#L5) |
| [`LOCAL_JAR`](#macro_LOCAL_JAR) | Macro | [build/conf/java.conf:744](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L744) |
| [`LOCAL_SOURCES_JAR`](#macro_LOCAL_SOURCES_JAR) | Macro | [build/conf/java.conf:749](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L749) |
| [`MACROS_WITH_ERROR`](#macro_MACROS_WITH_ERROR) | Macro | [build/plugins/macros_with_error.py:8](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L8) |
| [`MANUAL_GENERATION`](#macro_MANUAL_GENERATION) | Macro | [build/ymake.core.conf:3393](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3393) |
| [`NGINX_MODULES`](#macro_NGINX_MODULES) | Macro | [build/ymake.core.conf:5669](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5669) |
| [`NO_CLANG_TIDY`](#macro_NO_CLANG_TIDY) | Macro | [build/ymake.core.conf:4494](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4494) |
| [`NO_COW`](#macro_NO_COW) | Macro | [build/ymake.core.conf:900](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L900) |
| [`NO_MYPY`](#macro_NO_MYPY) | Macro | [build/conf/proto.conf:510](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L510) |
| [`NO_TS_TYPECHECK`](#macro_NO_TS_TYPECHECK) | Macro | [build/conf/ts/ts_test.conf:300](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L300) |
| [`NO_YMAKE_PYTHON3`](#macro_NO_YMAKE_PYTHON3) | Macro | [build/conf/python.conf:269](https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L269) |
| [`PIRE_INLINE`](#macro_PIRE_INLINE) | Macro | [build/ymake.core.conf:4152](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4152) |
| [`PIRE_INLINE_CMD`](#macro_PIRE_INLINE_CMD) | Macro | [build/ymake.core.conf:4147](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4147) |
| [`POPULATE_CPP_COVERAGE_FLAGS`](#macro_POPULATE_CPP_COVERAGE_FLAGS) | Macro | [build/conf/coverage_full_instrumentation.conf:7](https://a.yandex-team.ru/arcadia/build/conf/coverage_full_instrumentation.conf?rev=20020720#L7) |
| [`POPULATE_CPP_YNDEXING`](#macro_POPULATE_CPP_YNDEXING) | Macro | [build/conf/yndexing/cpp_instrumentation.conf:6](https://a.yandex-team.ru/arcadia/build/conf/yndexing/cpp_instrumentation.conf?rev=20020720#L6) |
| [`PROCESSOR_CLASSES`](#macro_PROCESSOR_CLASSES) | Macro | [build/conf/java.conf:118](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L118) |
| [`PROCESS_DOCS`](#macro_PROCESS_DOCS) | Macro | [build/plugins/docs.py:41](https://a.yandex-team.ru/arcadia/build/plugins/docs.py?rev=20020720#L41) |
| [`PROCESS_MKDOCS`](#macro_PROCESS_MKDOCS) | Macro | [build/internal/plugins/mkdocs.py:38](https://a.yandex-team.ru/arcadia/build/internal/plugins/mkdocs.py?rev=20020720#L38) |
| [`PROTO_CMD`](#macro_PROTO_CMD) | Macro | [build/conf/proto.conf:1077](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1077) |
| [`PY_ENUMS_SERIALIZATION`](#macro_PY_ENUMS_SERIALIZATION) | Macro | [build/plugins/pybuild.py:806](https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L806) |
| [`REGISTER_SANDBOX_IMPORT`](#macro_REGISTER_SANDBOX_IMPORT) | Macro | [build/internal/plugins/sandbox_registry.py:6](https://a.yandex-team.ru/arcadia/build/internal/plugins/sandbox_registry.py?rev=20020720#L6) |
| [`REGISTER_YQL_PYTHON_UDF`](#macro_REGISTER_YQL_PYTHON_UDF) | Macro | [build/plugins/yql_python_udf.py:4](https://a.yandex-team.ru/arcadia/build/plugins/yql_python_udf.py?rev=20020720#L4) |
| [`RESTRICT_PATH`](#macro_RESTRICT_PATH) | Macro | [build/plugins/macros_with_error.py:14](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L14) |
| [`RISK_GEN_DATA_MODEL`](#macro_RISK_GEN_DATA_MODEL) | Macro | [build/internal/plugins/fintech_risk_model.py:276](https://a.yandex-team.ru/arcadia/build/internal/plugins/fintech_risk_model.py?rev=20020720#L276) |
| [`RUN`](#macro_RUN) | Macro | [build/plugins/ytest.py:995](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L995) |
| [`RUN_JAVA_PROGRAM`](#macro_RUN_JAVA_PROGRAM) | Macro | [build/conf/java.conf:632](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L632) |
| [`SDBUS_CPP_ADAPTOR`](#macro_SDBUS_CPP_ADAPTOR) | Macro | [build/ymake.core.conf:5648](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5648) |
| [`SDBUS_CPP_PROXY`](#macro_SDBUS_CPP_PROXY) | Macro | [build/ymake.core.conf:5654](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5654) |
| [`SDC_DIAGS_SPLIT_GENERATOR_V3`](#macro_SDC_DIAGS_SPLIT_GENERATOR_V3) | Macro | [build/internal/plugins/sdc_diagnostics.py:61](https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_diagnostics.py?rev=20020720#L61) |
| [`SDC_DIAGS_SPLIT_GENERATOR_V4`](#macro_SDC_DIAGS_SPLIT_GENERATOR_V4) | Macro | [build/internal/plugins/sdc_diagnostics.py:24](https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_diagnostics.py?rev=20020720#L24) |
| [`SETUP_EXECTEST`](#macro_SETUP_EXECTEST) | Macro | [build/plugins/_dart_fields.py:61](https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61) |
| [`SETUP_PYTEST_BIN`](#macro_SETUP_PYTEST_BIN) | Macro | [build/plugins/ytest.py:987](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L987) |
| [`SETUP_RUN_PYTHON`](#macro_SETUP_RUN_PYTHON) | Macro | [build/plugins/ytest.py:1041](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L1041) |
| [`SET_COMPILE_OUTPUTS_MODIFIERS`](#macro_SET_COMPILE_OUTPUTS_MODIFIERS) | Macro | [build/ymake.core.conf:3192](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3192) |
| [`SET_CPP_COVERAGE_FLAGS`](#macro_SET_CPP_COVERAGE_FLAGS) | Macro | [build/plugins/coverage.py:43](https://a.yandex-team.ru/arcadia/build/plugins/coverage.py?rev=20020720#L43) |
| [`SET_CUSTOM_CLANG_TIDY`](#macro_SET_CUSTOM_CLANG_TIDY) | Macro | [build/ymake.core.conf:1189](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1189) |
| [`SRC_RESOURCE`](#macro_SRC_RESOURCE) | Macro | [build/conf/java.conf:739](https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L739) |
| [`STYLE_DETEKT`](#macro_STYLE_DETEKT) | Macro | [build/conf/custom_lint.conf:52](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L52) |
| [`STYLE_YAML`](#macro_STYLE_YAML) | Macro | [build/conf/custom_lint.conf:23](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L23) |
| [`STYLE_YQL`](#macro_STYLE_YQL) | Macro | [build/conf/custom_lint.conf:33](https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L33) |
| [`TASKLET`](#macro_TASKLET) | Macro | [build/ymake.core.conf:5346](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5346) |
| [`TASKLET_REG`](#macro_TASKLET_REG) | Macro | [build/ymake.core.conf:5363](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5363) |
| [`TASKLET_REG_EXT`](#macro_TASKLET_REG_EXT) | Macro | [build/ymake.core.conf:5378](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5378) |
| [`TEST_DATA`](#macro_TEST_DATA) | Macro | [build/plugins/ytest.py:118](https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L118) |
| [`TS_BUILD_OUTPUTS`](#macro_TS_BUILD_OUTPUTS) | Macro | [build/conf/ts/ts_library.conf:66](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L66) |
| [`TS_BUILD_SCRIPT`](#macro_TS_BUILD_SCRIPT) | Macro | [build/conf/ts/ts_library.conf:62](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L62) |
| [`TS_LINT`](#macro_TS_LINT) | Macro | [build/conf/ts/ts_check.conf:8](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L8) |
| [`TS_TEST`](#macro_TS_TEST) | Macro | [build/conf/ts/ts_check.conf:12](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L12) |
| [`UPDATE_VCS_JAVA_INFO_NODEP`](#macro_UPDATE_VCS_JAVA_INFO_NODEP) | Macro | [build/ymake.core.conf:4203](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4203) |
| [`USE_COMMON_GOOGLE_APIS`](#macro_USE_COMMON_GOOGLE_APIS) | Macro | [build/conf/proto.conf:363](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L363) |
| [`USE_LEGACY_PNPM_VIRTUAL_STORE`](#macro_USE_LEGACY_PNPM_VIRTUAL_STORE) | Macro | [build/conf/ts/node_modules.conf:80](https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L80) |
| [`USE_LLVM_BC16`](#macro_USE_LLVM_BC16) | Macro | [build/ymake.core.conf:4953](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4953) |
| [`USE_LLVM_BC18`](#macro_USE_LLVM_BC18) | Macro | [build/ymake.core.conf:4958](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4958) |
| [`USE_LLVM_BC20`](#macro_USE_LLVM_BC20) | Macro | [build/ymake.core.conf:4963](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4963) |
| [`USRV_GEN_GRPC_CLIENT_V2`](#macro_USRV_GEN_GRPC_CLIENT_V2) | Macro | [build/internal/plugins/userver.py:338](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L338) |
| [`USRV_GEN_GRPC_CLIENT_V2_STRUCTS`](#macro_USRV_GEN_GRPC_CLIENT_V2_STRUCTS) | Macro | [build/internal/plugins/userver.py:343](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L343) |
| [`USRV_GEN_GRPC_SERVICE_V2`](#macro_USRV_GEN_GRPC_SERVICE_V2) | Macro | [build/internal/plugins/userver.py:348](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L348) |
| [`USRV_GEN_GRPC_SERVICE_V2_STRUCTS`](#macro_USRV_GEN_GRPC_SERVICE_V2_STRUCTS) | Macro | [build/internal/plugins/userver.py:353](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L353) |
| [`USRV_GEN_PROTO_STRUCTS`](#macro_USRV_GEN_PROTO_STRUCTS) | Macro | [build/internal/plugins/userver.py:318](https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L318) |
| [`VALIDATE_IN_DIRS`](#macro_VALIDATE_IN_DIRS) | Macro | [build/plugins/macros_with_error.py:38](https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L38) |
| [`VITE_OUTPUT`](#macro_VITE_OUTPUT) | Macro | [build/conf/ts/ts_vite.conf:30](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L30) |
| [`WEBPACK_OUTPUT`](#macro_WEBPACK_OUTPUT) | Macro | [build/conf/ts/ts_webpack.conf:28](https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L28) |
| [`WINDOWS_LONG_PATH_MANIFEST`](#macro_WINDOWS_LONG_PATH_MANIFEST) | Macro | [build/ymake.core.conf:5611](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5611) |
| [`WINDOWS_MANIFEST`](#macro_WINDOWS_MANIFEST) | Macro | [build/ymake.core.conf:5606](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5606) |
| [`WITHOUT_VERSION`](#macro_WITHOUT_VERSION) | Macro | [build/ymake.core.conf:5802](https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5802) |
| [`WITH_KOTLIN_GRPC`](#macro_WITH_KOTLIN_GRPC) | Macro | [build/conf/proto.conf:231](https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L231) |
| [`YABS_GENERATE_CONF`](#macro_YABS_GENERATE_CONF) | Macro | [build/internal/plugins/yabs_generate_conf.py:11](https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L11) |
| [`YABS_GENERATE_PHANTOM_CONF_PATCH`](#macro_YABS_GENERATE_PHANTOM_CONF_PATCH) | Macro | [build/internal/plugins/yabs_generate_conf.py:43](https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L43) |
| [`YABS_GENERATE_PHANTOM_CONF_TEST_CHECK`](#macro_YABS_GENERATE_PHANTOM_CONF_TEST_CHECK) | Macro | [build/internal/plugins/yabs_generate_conf.py:54](https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L54) |
| [`YT_RECORD_DISABLE_PEERDIR`](#macro_YT_RECORD_DISABLE_PEERDIR) | Macro | [build/conf/project_specific/yt.conf:3](https://a.yandex-team.ru/arcadia/build/conf/project_specific/yt.conf?rev=20020720#L3) |
| [`ALLOWED_IN_LINTERS_MAKE`](#property_ALLOWED_IN_LINTERS_MAKE) | Property | [devtools/ymake/lang/properties.h:35](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L35) |
| [`EXTS`](#property_EXTS) | Property | [devtools/ymake/lang/properties.h:47](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L47) |
| [`PROXY`](#property_PROXY) | Property | [devtools/ymake/lang/properties.h:81](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L81) |
| [`SYMLINK_POLICY`](#property_SYMLINK_POLICY) | Property | [devtools/ymake/lang/properties.h:89](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L89) |
| [`USE_PEERS_LATE_OUTS`](#property_USE_PEERS_LATE_OUTS) | Property | [devtools/ymake/lang/properties.h:90](https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L90) |
| [`APPLIED_EXCLUDES`](#variable_APPLIED_EXCLUDES) | Variable | [devtools/ymake/vardefs.h:23](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L23) |
| [`AUTO_INPUT`](#variable_AUTO_INPUT) | Variable | [devtools/ymake/vardefs.h:26](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L26) |
| [`CHECK_INTERNAL`](#variable_CHECK_INTERNAL) | Variable | [devtools/ymake/vardefs.h:28](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L28) |
| [`CMAKE_CURRENT_BINARY_DIR`](#variable_CMAKE_CURRENT_BINARY_DIR) | Variable | [devtools/ymake/vardefs.h:29](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L29) |
| [`CMAKE_CURRENT_SOURCE_DIR`](#variable_CMAKE_CURRENT_SOURCE_DIR) | Variable | [devtools/ymake/vardefs.h:30](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L30) |
| [`CONSUME_NON_MANAGEABLE_PEERS`](#variable_CONSUME_NON_MANAGEABLE_PEERS) | Variable | [devtools/ymake/vardefs.h:31](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L31) |
| [`DART_CLASSPATH`](#variable_DART_CLASSPATH) | Variable | [devtools/ymake/vardefs.h:34](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L34) |
| [`DART_CLASSPATH_DEPS`](#variable_DART_CLASSPATH_DEPS) | Variable | [devtools/ymake/vardefs.h:33](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L33) |
| [`DEPENDENCY_MANAGEMENT_VALUE`](#variable_DEPENDENCY_MANAGEMENT_VALUE) | Variable | [devtools/ymake/vardefs.h:36](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L36) |
| [`DONT_RESOLVE_INCLUDES`](#variable_DONT_RESOLVE_INCLUDES) | Variable | [devtools/ymake/vardefs.h:39](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L39) |
| [`DYNAMIC_LINK`](#variable_DYNAMIC_LINK) | Variable | [devtools/ymake/vardefs.h:40](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L40) |
| [`EV_HEADER_EXTS`](#variable_EV_HEADER_EXTS) | Variable | [devtools/ymake/vardefs.h:41](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L41) |
| [`EXCLUDE_SUBMODULES`](#variable_EXCLUDE_SUBMODULES) | Variable | [devtools/ymake/vardefs.h:42](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L42) |
| [`EXCLUDE_VALUE`](#variable_EXCLUDE_VALUE) | Variable | [devtools/ymake/vardefs.h:43](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L43) |
| [`EXPORTED_BUILD_SYSTEM_BUILD_ROOT`](#variable_EXPORTED_BUILD_SYSTEM_BUILD_ROOT) | Variable | [devtools/ymake/vardefs.h:44](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L44) |
| [`EXPORTED_BUILD_SYSTEM_SOURCE_ROOT`](#variable_EXPORTED_BUILD_SYSTEM_SOURCE_ROOT) | Variable | [devtools/ymake/vardefs.h:45](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L45) |
| [`GLOBAL_SUFFIX`](#variable_GLOBAL_SUFFIX) | Variable | [devtools/ymake/vardefs.h:47](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L47) |
| [`GLOBAL_TARGET`](#variable_GLOBAL_TARGET) | Variable | [devtools/ymake/vardefs.h:48](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L48) |
| [`GO_HAS_INTERNAL_TESTS`](#variable_GO_HAS_INTERNAL_TESTS) | Variable | [devtools/ymake/vardefs.h:49](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L49) |
| [`GO_TEST_FOR_DIR`](#variable_GO_TEST_FOR_DIR) | Variable | [devtools/ymake/vardefs.h:50](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L50) |
| [`HAS_MANAGEABLE_PEERS`](#variable_HAS_MANAGEABLE_PEERS) | Variable | [devtools/ymake/vardefs.h:51](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L51) |
| [`IGNORE_JAVA_DEPENDENCIES_CONFIGURATION`](#variable_IGNORE_JAVA_DEPENDENCIES_CONFIGURATION) | Variable | [devtools/ymake/vardefs.h:52](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L52) |
| [`INPUT`](#variable_INPUT) | Variable | [devtools/ymake/vardefs.h:53](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L53) |
| [`INTERNAL_EXCEPTIONS`](#variable_INTERNAL_EXCEPTIONS) | Variable | [devtools/ymake/vardefs.h:54](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L54) |
| [`JAVA_DEPENDENCIES_CONFIGURATION_VALUE`](#variable_JAVA_DEPENDENCIES_CONFIGURATION_VALUE) | Variable | [devtools/ymake/vardefs.h:55](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L55) |
| [`MANAGED_PEERS`](#variable_MANAGED_PEERS) | Variable | [devtools/ymake/vardefs.h:57](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L57) |
| [`MANAGED_PEERS_CLOSURE`](#variable_MANAGED_PEERS_CLOSURE) | Variable | [devtools/ymake/vardefs.h:56](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L56) |
| [`MANGLED_MODULE_TYPE`](#variable_MANGLED_MODULE_TYPE) | Variable | [devtools/ymake/vardefs.h:58](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L58) |
| [`MODULE_ARGS`](#variable_MODULE_ARGS) | Variable | [devtools/ymake/vardefs.h:60](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L60) |
| [`MODULE_COMMON_CONFIGS_DIR`](#variable_MODULE_COMMON_CONFIGS_DIR) | Variable | [devtools/ymake/vardefs.h:61](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L61) |
| [`MODULE_KIND`](#variable_MODULE_KIND) | Variable | [devtools/ymake/vardefs.h:62](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L62) |
| [`MODULE_LANG`](#variable_MODULE_LANG) | Variable | [devtools/ymake/vardefs.h:63](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L63) |
| [`MODULE_PREFIX`](#variable_MODULE_PREFIX) | Variable | [devtools/ymake/vardefs.h:64](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L64) |
| [`MODULE_SUFFIX`](#variable_MODULE_SUFFIX) | Variable | [devtools/ymake/vardefs.h:65](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L65) |
| [`MODULE_TYPE`](#variable_MODULE_TYPE) | Variable | [devtools/ymake/vardefs.h:69](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L69) |
| [`NON_NAMAGEABLE_PEERS`](#variable_NON_NAMAGEABLE_PEERS) | Variable | [devtools/ymake/vardefs.h:72](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L72) |
| [`OUTPUT`](#variable_OUTPUT) | Variable | [devtools/ymake/vardefs.h:73](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L73) |
| [`PEERDIR_TAGS`](#variable_PEERDIR_TAGS) | Variable | [devtools/ymake/vardefs.h:77](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L77) |
| [`PEERS_LATE_OUTS`](#variable_PEERS_LATE_OUTS) | Variable | [devtools/ymake/vardefs.h:78](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L78) |
| [`PROTO_HEADER_EXTS`](#variable_PROTO_HEADER_EXTS) | Variable | [devtools/ymake/vardefs.h:80](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L80) |
| [`PYTHON_BIN`](#variable_PYTHON_BIN) | Variable | [devtools/ymake/vardefs.h:81](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L81) |
| [`REALPRJNAME`](#variable_REALPRJNAME) | Variable | [devtools/ymake/vardefs.h:82](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L82) |
| [`SONAME`](#variable_SONAME) | Variable | [devtools/ymake/vardefs.h:83](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L83) |
| [`SRCS_GLOBAL`](#variable_SRCS_GLOBAL) | Variable | [devtools/ymake/vardefs.h:85](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L85) |
| [`START_TARGET`](#variable_START_TARGET) | Variable | [devtools/ymake/vardefs.h:86](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L86) |
| [`TARGET`](#variable_TARGET) | Variable | [devtools/ymake/vardefs.h:87](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L87) |
| [`TEST_CASE_ROOT`](#variable_TEST_CASE_ROOT) | Variable | [devtools/ymake/vardefs.h:88](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L88) |
| [`TEST_OUT_ROOT`](#variable_TEST_OUT_ROOT) | Variable | [devtools/ymake/vardefs.h:89](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L89) |
| [`TEST_SOURCE_ROOT`](#variable_TEST_SOURCE_ROOT) | Variable | [devtools/ymake/vardefs.h:90](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L90) |
| [`TEST_WORK_ROOT`](#variable_TEST_WORK_ROOT) | Variable | [devtools/ymake/vardefs.h:91](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L91) |
| [`TOOLS`](#variable_TOOLS) | Variable | [devtools/ymake/vardefs.h:92](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L92) |
| [`TS_CONFIG_DECLARATION`](#variable_TS_CONFIG_DECLARATION) | Variable | [devtools/ymake/vardefs.h:94](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L94) |
| [`TS_CONFIG_DECLARATION_MAP`](#variable_TS_CONFIG_DECLARATION_MAP) | Variable | [devtools/ymake/vardefs.h:93](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L93) |
| [`TS_CONFIG_DEDUCE_OUT`](#variable_TS_CONFIG_DEDUCE_OUT) | Variable | [devtools/ymake/vardefs.h:95](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L95) |
| [`TS_CONFIG_OUT_DIR`](#variable_TS_CONFIG_OUT_DIR) | Variable | [devtools/ymake/vardefs.h:96](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L96) |
| [`TS_CONFIG_PRESERVE_JSX`](#variable_TS_CONFIG_PRESERVE_JSX) | Variable | [devtools/ymake/vardefs.h:97](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L97) |
| [`TS_CONFIG_ROOT_DIR`](#variable_TS_CONFIG_ROOT_DIR) | Variable | [devtools/ymake/vardefs.h:98](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L98) |
| [`TS_CONFIG_SOURCE_MAP`](#variable_TS_CONFIG_SOURCE_MAP) | Variable | [devtools/ymake/vardefs.h:99](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L99) |
| [`UNITTEST_DIR`](#variable_UNITTEST_DIR) | Variable | [devtools/ymake/vardefs.h:100](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L100) |
| [`UNITTEST_MOD`](#variable_UNITTEST_MOD) | Variable | [devtools/ymake/vardefs.h:101](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L101) |
| [`USE_ALL_SRCS`](#variable_USE_ALL_SRCS) | Variable | [devtools/ymake/vardefs.h:102](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L102) |
| [`USE_GLOBAL_CMD`](#variable_USE_GLOBAL_CMD) | Variable | [devtools/ymake/vardefs.h:103](https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L103) |

</details>

 [DLL\_JAVA]: https://a.yandex-team.ru/arcadia/build/conf/swig.conf?rev=20020720#L90
 [DOCS]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L155
 [FBS\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L113
 [JAVA\_ANNOTATION\_PROCESSOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L136
 [JAVA\_CONTRIB\_ANNOTATION\_PROCESSOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L154
 [JAVA\_CONTRIB\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L418
 [JAVA\_LIBRARY\_SPLIT]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L16
 [JAVA\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L85
 [JTEST]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L317
 [JTEST\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L377
 [JUNIT5]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L255
 [JUNIT6]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L191
 [PACKAGE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2516
 [PROTO\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L917
 [PROTO\_SCHEMA]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1004
 [PY23\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1211
 [PY23\_NATIVE\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1237
 [PY23\_TEST]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1263
 [PY3TEST]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L515
 [PY3\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L347
 [TS\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L39
 [TS\_NEXT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L74
 [TS\_PACKAGE]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_package.conf?rev=20020720#L13
 [TS\_RSPACK]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L49
 [TS\_TEST\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L16
 [TS\_TSC]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_tsc.conf?rev=20020720#L22
 [TS\_VITE]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L58
 [TS\_WEBPACK]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L56
 [YQL\_UDF]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L183
 [YQL\_UDF\_CONTRIB]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L225
 [YQL\_UDF\_YDB]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L204
 [BOOSTTEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1520
 [BOOSTTEST\_WITH\_MAIN]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1542
 [CI\_GROUP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2545
 [CUDA\_DEVICE\_LINK\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L132
 [DEFAULT\_IOS\_INTERFACE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5544
 [DLL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2307
 [DLL\_TOOL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2324
 [DOCS\_HTML]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L236
 [DOCS\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L88
 [EXECTEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1816
 [FAT\_OBJECT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2105
 [FUZZ]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1476
 [GEN\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L594
 [GO\_DLL]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1119
 [GO\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L993
 [GO\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1010
 [GO\_TEST]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1139
 [GTEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1452
 [G\_BENCHMARK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1854
 [IOS\_INTERFACE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5533
 [JAVA\_CONTRIB]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L765
 [JAVA\_CONTRIB\_PROXY]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L709
 [JAVA\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L40
 [JAVA\_TEST\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L44
 [LIBRARY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1989
 [PROGRAM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1303
 [PROTO\_DESCRIPTIONS]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L976
 [PROTO\_REGISTRY]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L989
 [PY2MODULE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L602
 [PY2TEST]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L463
 [PY2\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L694
 [PY2\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L315
 [PY3MODULE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L621
 [PY3TEST\_BIN]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L486
 [PY3\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L738
 [PY3\_PROGRAM\_BIN]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L915
 [PYTEST\_BIN]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L445
 [PY\_ANY\_MODULE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L544
 [RECURSIVE\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2164
 [RESOURCES\_LIBRARY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2064
 [R\_MODULE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2277
 [SO\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2336
 [TS\_TEST\_HERMIONE\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L95
 [TS\_TEST\_JEST\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L30
 [TS\_TEST\_PLAYWRIGHT\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L129
 [TS\_TEST\_PLAYWRIGHT\_LARGE\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L160
 [TS\_TEST\_VITEST\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L63
 [UNION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2567
 [UNITTEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1397
 [UNITTEST\_FOR]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1898
 [UNITTEST\_WITH\_CUSTOM\_ENTRY\_POINT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1433
 [YQL\_PYTHON3\_UDF]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L326
 [YQL\_PYTHON3\_UDF\_TEST]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L377
 [YQL\_PYTHON\_UDF]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L269
 [YQL\_PYTHON\_UDF\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L298
 [YQL\_PYTHON\_UDF\_TEST]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L363
 [YQL\_UDF\_MINITEST]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L65
 [YQL\_UDF\_MODULE]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L150
 [YQL\_UDF\_MODULE\_CONTRIB]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L162
 [YQL\_UDF\_TEST]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L43
 [YQL\_UDF\_YDB\_MODULE]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L156
 [YT\_UNITTEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1423
 [Y\_BENCHMARK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1836
 [ACCELEO]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L5
 [ADDINCL]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [ADDINCLSELF]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3179
 [ADD\_CHECK]: https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L772
 [ADD\_CHECK\_PY\_IMPORTS]: https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61
 [ADD\_CLANG\_TIDY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1185
 [ADD\_COMPILABLE\_TRANSLATE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2774
 [ADD\_COMPILABLE\_TRANSLIT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2784
 [ADD\_DLLS\_TO\_JAR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2149
 [ADD\_IWYU]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1197
 [ADD\_PYTEST\_BIN]: https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61
 [ADD\_YTEST]: https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L1567
 [ALICE\_GENERATE\_FUNCTION\_PROTO\_INCLUDES]: https://a.yandex-team.ru/arcadia/build/internal/plugins/alice.py?rev=20020720#L94
 [ALICE\_GENERATE\_FUNCTION\_SPECS]: https://a.yandex-team.ru/arcadia/build/internal/plugins/alice.py?rev=20020720#L47
 [ALLOCATOR]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2672
 [ALLOCATOR\_IMPL]: https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L113
 [ALL\_GO\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L410
 [ALL\_PYTEST\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1169
 [ALL\_PY\_EXTRA\_LINT\_FILES]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1189
 [ALL\_PY\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1151
 [ALL\_RESOURCE\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2863
 [ALL\_RESOURCE\_FILES\_FROM\_DIRS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2878
 [ALL\_SRCS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2454
 [ANNOTATION\_PROCESSOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2083
 [ARCHIVE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4163
 [ARCHIVE\_ASM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4141
 [ARCHIVE\_BY\_KEYS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4174
 [AR\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3417
 [ASM\_PREINCLUDE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5240
 [ASSERT]: https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L30
 [AUTO\_SERVICE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L122
 [BENCHMARK\_OPTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1873
 [BISON\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L57
 [BISON\_GEN\_C]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L64
 [BISON\_GEN\_CPP]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L72
 [BISON\_HEADER]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L94
 [BISON\_NO\_HEADER]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L104
 [BPF]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5009
 [BPF\_STATIC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5023
 [BUILDWITH\_CYTHON\_C]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4047
 [BUILDWITH\_CYTHON\_CPP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4016
 [BUILDWITH\_RAGEL6]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4085
 [BUILD\_CATBOOST]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/other.conf?rev=20020720#L9
 [BUILD\_ONLY\_IF]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [BUILD\_YDL\_DESC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3753
 [BUNDLE]: https://a.yandex-team.ru/arcadia/build/plugins/bundle.py?rev=20020720#L6
 [BUNDLE\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2903
 [CFLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4281
 [CGO\_CFLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L461
 [CGO\_LDFLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L470
 [CGO\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L444
 [CHECK\_ALLOWED\_PATH]: https://a.yandex-team.ru/arcadia/build/internal/plugins/container_layers.py?rev=20020720#L5
 [CHECK\_CONTRIB\_CREDITS]: https://a.yandex-team.ru/arcadia/build/plugins/credits.py?rev=20020720#L11
 [CHECK\_DEPENDENT\_DIRS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L487
 [CHECK\_JAVA\_DEPS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1838
 [CLANG\_EMIT\_AST\_CXX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4972
 [CLANG\_EMIT\_AST\_CXX\_RUN\_TOOL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5785
 [CLANG\_WARNINGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5754
 [CLEAN\_TEXTREL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2197
 [CMAKE\_EXPORTED\_TARGET\_NAME]: https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L108
 [COLLECT\_CONFIG\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5595
 [COLLECT\_FRONTEND\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5581
 [COLLECT\_GO\_SWAGGER\_FILES]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L5
 [COLLECT\_JINJA\_TEMPLATES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5474
 [COLLECT\_KOTLIN\_LINT\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L43
 [COLLECT\_YAML\_CONFIG\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5588
 [COMPILE\_C\_AS\_CXX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4930
 [COMPILE\_LUA]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3676
 [COMPILE\_LUA\_21]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3695
 [COMPILE\_LUA\_OPENRESTY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3713
 [CONFIGURE\_FILE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4260
 [CONFTEST\_LOAD\_POLICY\_LEGACY\_GLOBAL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1718
 [CONFTEST\_LOAD\_POLICY\_LOCAL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1706
 [CONLYFLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4304
 [COPY]: https://a.yandex-team.ru/arcadia/build/plugins/cp.py?rev=20020720#L6
 [COPY\_FILE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2819
 [COPY\_FILE\_WITH\_CONTEXT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2839
 [COW]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L896
 [CPP\_ADDINCL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5332
 [CPP\_ENUMS\_SERIALIZATION]: https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L824
 [CPP\_EVLOG]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L605
 [CPP\_EV\_PROTO\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L283
 [CPP\_PROTOLIBS\_DEBUG\_INFO]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L74
 [CPP\_PROTO\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L273
 [CPP\_PROTO\_PLUGIN0]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L259
 [CPP\_PROTO\_PLUGIN2]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L294
 [CREATE\_BUILDINFO\_FOR]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4230
 [CREATE\_INIT\_PY\_STRUCTURE]: https://a.yandex-team.ru/arcadia/build/plugins/create_init_py.py?rev=20020720#L6
 [CREDITS\_DISCLAIMER]: https://a.yandex-team.ru/arcadia/build/plugins/credits.py?rev=20020720#L5
 [CTEMPLATE\_VARNAMES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4946
 [CUDA\_NVCC\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L39
 [CUDA\_SRCS]: https://a.yandex-team.ru/arcadia/build/plugins/cuda.py?rev=20020720#L15
 [CUSTOM\_LINK\_STEP\_SCRIPT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1356
 [CXXFLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4311
 [CYTHON\_FLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4318
 [DARWIN\_SIGNED\_RESOURCE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5524
 [DARWIN\_STRINGS\_RESOURCE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5520
 [DATA]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1618
 [DATA\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1644
 [DEB\_VERSION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4557
 [DECIMAL\_MD5\_LOWER\_32\_BITS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4243
 [DECLARE\_EXTERNAL\_HOST\_RESOURCES\_BUNDLE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DECLARE\_EXTERNAL\_HOST\_RESOURCES\_BUNDLE\_BY\_JSON]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DECLARE\_EXTERNAL\_HOST\_RESOURCES\_PACK]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DECLARE\_EXTERNAL\_RESOURCE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DECLARE\_EXTERNAL\_RESOURCE\_BY\_JSON]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DECLARE\_IN\_DIRS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4751
 [DEFAULT]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DEFAULT\_JAVA\_SRCS\_LAYOUT]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L565
 [DEFAULT\_JDK\_VERSION]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2469
 [DEFAULT\_JUNIT\_JAVA\_SRCS\_LAYOUT]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L583
 [DEPENDENCY\_MANAGEMENT]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2178
 [DEPENDS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DIRECT\_DEPS\_ONLY]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2347
 [DISABLE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DISABLE\_DATA\_VALIDATION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1602
 [DLL\_FOR]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [DOCKER\_IMAGE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1634
 [DOCS\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L327
 [DOCS\_COPY\_FILES]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L13
 [DOCS\_DIR]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L288
 [DOCS\_HTML\_FROM]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L254
 [DOCS\_INCLUDE\_SOURCES]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L351
 [DOCS\_VARS]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L339
 [DYNAMIC\_LIBRARY\_FROM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2294
 [ELSE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [ELSEIF]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [EMBED\_JAVA\_VCS\_INFO]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L453
 [ENABLE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [ENABLE\_PREVIEW]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2047
 [END]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [ENDIF]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [ENV]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1687
 [EVLOG\_CMD]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1072
 [EXCLUDE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2091
 [EXCLUDE\_TAGS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [EXPERIMENTAL\_FORK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3008
 [EXPLICIT\_DATA]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1654
 [EXPLICIT\_OUTPUTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5151
 [EXPORTS\_SCRIPT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1320
 [EXPORT\_ALL\_DYNAMIC\_SYMBOLS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1338
 [EXTERNAL\_RESOURCE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [EXTRADIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [EXTRALIBS\_STATIC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2767
 [FBS\_CMD]: https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L153
 [FBS\_NAMESPACE]: https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L95
 [FBS\_TO\_PY2SRC]: https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L28
 [FILES]: https://a.yandex-team.ru/arcadia/build/plugins/files.py?rev=20020720#L4
 [FLATC\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L10
 [FLAT\_JOIN\_SRCS\_GLOBAL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3050
 [FLEX\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L50
 [FLEX\_GEN\_C]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L80
 [FLEX\_GEN\_CPP]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L87
 [FORK\_SUBTESTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2970
 [FORK\_TESTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2956
 [FORK\_TEST\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2996
 [FROM\_ARCHIVE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4915
 [FROM\_SANDBOX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4895
 [FULL\_JAVA\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L553
 [FUNCTION\_ORDERING\_FILE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L151
 [FUZZ\_DICTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1554
 [FUZZ\_OPTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1573
 [GENERATE\_ENUM\_SERIALIZATION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4536
 [GENERATE\_ENUM\_SERIALIZATION\_WITH\_HEADER]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4548
 [GENERATE\_IMPLIB]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5840
 [GENERATE\_PY\_PROTOS]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L671
 [GENERATE\_SCRIPT]: https://a.yandex-team.ru/arcadia/build/plugins/java.py?rev=20020720#L297
 [GENERATE\_YT\_RECORD]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yt.conf?rev=20020720#L7
 [GEN\_SCHEEME2]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4643
 [GLOBAL\_CFLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4289
 [GLOBAL\_SRCS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2436
 [GOLANG\_VERSION]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L187
 [GO\_ASM\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L149
 [GO\_BENCH\_TIMEOUT]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1128
 [GO\_CGO1\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L157
 [GO\_CGO2\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L165
 [GO\_COMPILE\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L173
 [GO\_EMBED\_BINDIR]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L576
 [GO\_EMBED\_DIR]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L543
 [GO\_EMBED\_PATTERN]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L507
 [GO\_EMBED\_TEST\_DIR]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L551
 [GO\_EMBED\_XTEST\_DIR]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L559
 [GO\_GRPC\_GATEWAY\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L658
 [GO\_GRPC\_GATEWAY\_SWAGGER\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L666
 [GO\_GRPC\_GATEWAY\_V2\_OPENAPI\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L718
 [GO\_GRPC\_GATEWAY\_V2\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L736
 [GO\_LDFLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L453
 [GO\_LINK\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L181
 [GO\_MOCKGEN\_CONTRIB\_FROM]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1207
 [GO\_MOCKGEN\_FROM]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1197
 [GO\_MOCKGEN\_MOCKS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1258
 [GO\_MOCKGEN\_PACKAGE]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1223
 [GO\_MOCKGEN\_REFLECT]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1232
 [GO\_MOCKGEN\_SOURCE]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1287
 [GO\_MOCKGEN\_TYPES]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1213
 [GO\_OAPI\_CODEGEN]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1303
 [GO\_OAPI\_CODEGEN\_TAXI]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1326
 [GO\_OAPI\_CODEGEN\_TAXI\_1134]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1332
 [GO\_OAPI\_CODEGEN\_V2]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L1320
 [GO\_PACKAGE\_NAME]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L385
 [GO\_PROTO\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L412
 [GO\_PROTO\_USE\_V2]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L648
 [GO\_SKIP\_TESTS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L481
 [GO\_SSO]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L219
 [GO\_SSO\_TOOL]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L232
 [GO\_TEST\_EMBED\_BINDIR]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L584
 [GO\_TEST\_EMBED\_PATTERN]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L515
 [GO\_TEST\_FOR]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [GO\_TEST\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L420
 [GO\_XTEST\_EMBED\_BINDIR]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L592
 [GO\_XTEST\_EMBED\_PATTERN]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L523
 [GO\_XTEST\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/go.conf?rev=20020720#L430
 [GRPC]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L620
 [GRPC\_WITH\_GMOCK]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L641
 [HEADERS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5764
 [IDEA\_EXCLUDE\_DIRS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2004
 [IDEA\_MODULE\_NAME]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2024
 [IDEA\_RESOURCE\_DIRS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2014
 [IF]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [INCLUDE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [INCLUDE\_ONCE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [INCLUDE\_TAGS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [INDUCED\_DEPS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [INJECT\_PEERS]: https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L84
 [IOS\_APP\_ASSETS\_FLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5516
 [IOS\_APP\_COMMON\_FLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5510
 [IOS\_APP\_SETTINGS]: https://a.yandex-team.ru/arcadia/build/plugins/ios_app_settings.py?rev=20020720#L5
 [IOS\_ASSETS]: https://a.yandex-team.ru/arcadia/build/plugins/ios_assets.py?rev=20020720#L6
 [IWYU\_MAPPING\_FILE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4502
 [JAR\_ANNOTATION\_PROCESSOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L640
 [JAR\_EXCLUDE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2398
 [JAR\_MAIN\_CLASS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1146
 [JAR\_RESOURCE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L734
 [JAVAC\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2035
 [JAVA\_DEPENDENCIES\_CONFIGURATION]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2387
 [JAVA\_EXTERNAL\_DEPENDENCIES]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2356
 [JAVA\_IGNORE\_CLASSPATH\_CLASH\_FOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L612
 [JAVA\_MODULE]: https://a.yandex-team.ru/arcadia/build/plugins/java.py?rev=20020720#L41
 [JAVA\_PROTO\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L217
 [JAVA\_RESOURCE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1040
 [JAVA\_RESOURCE\_TAR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2140
 [JAVA\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2128
 [JAVA\_TEST]: https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61
 [JAVA\_TEST\_DEPS]: https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61
 [JDK\_VERSION]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2461
 [JNI\_EXPORTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1324
 [JOIN\_SRCS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3029
 [JOIN\_SRCS\_GLOBAL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3040
 [JUNIT\_TESTS\_JAR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L182
 [JVM\_ARGS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1827
 [KAPT\_ANNOTATION\_PROCESSOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L863
 [KAPT\_ANNOTATION\_PROCESSOR\_CLASSPATH]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L871
 [KAPT\_ANNOTATION\_PROCESSOR\_OPTIONS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L881
 [KAPT\_OPTS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L855
 [KOTLINC\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2233
 [KTLINT\_BASELINE\_FILE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2496
 [KTLINT\_RULESET]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2504
 [LARGE\_FILES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4906
 [LDFLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4271
 [LD\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3426
 [LICENSE]: https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L26
 [LICENSE\_RESTRICTION]: https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L43
 [LICENSE\_RESTRICTION\_EXCEPTIONS]: https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L66
 [LICENSE\_TEXTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5692
 [LINKER\_SCRIPT]: https://a.yandex-team.ru/arcadia/build/plugins/linker_script.py?rev=20020720#L4
 [LINK\_EXCLUDE\_LIBRARIES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5824
 [LINT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1788
 [LIST\_PROTO]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L699
 [LJ\_21\_ARCHIVE]: https://a.yandex-team.ru/arcadia/build/plugins/lj_archive.py?rev=20020720#L29
 [LJ\_ARCHIVE]: https://a.yandex-team.ru/arcadia/build/plugins/lj_archive.py?rev=20020720#L4
 [LLVM\_BC]: https://a.yandex-team.ru/arcadia/build/plugins/llvm_bc.py?rev=20020720#L5
 [LLVM\_COMPILE\_C]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4995
 [LLVM\_COMPILE\_CXX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4981
 [LLVM\_COMPILE\_LL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5032
 [LLVM\_LINK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5042
 [LLVM\_LLC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5063
 [LLVM\_OPT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5052
 [LOCAL\_JAR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L744
 [LOCAL\_SOURCES\_JAR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L749
 [MACROS\_WITH\_ERROR]: https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L8
 [MANUAL\_GENERATION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3393
 [MASMFLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4296
 [MAVEN\_GROUP\_ID]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2072
 [MESSAGE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [MODULEWISE\_LICENSE\_RESTRICTION]: https://a.yandex-team.ru/arcadia/build/conf/license.conf?rev=20020720#L58
 [NEED\_CHECK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4566
 [NEED\_REVIEW]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4584
 [NGINX\_MODULES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5669
 [NO\_BUILD\_IF]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [NO\_CHECK\_IMPORTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5160
 [NO\_CLANG\_COVERAGE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4474
 [NO\_CLANG\_MCDC\_COVERAGE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4482
 [NO\_CLANG\_TIDY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4494
 [NO\_COMPILER\_WARNINGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4347
 [NO\_COW]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L900
 [NO\_CPU\_CHECK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3172
 [NO\_CUDA\_NVPRUNE]: https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L152
 [NO\_CYTHON\_COVERAGE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1094
 [NO\_DEBUG\_INFO]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4937
 [NO\_DOCTESTS]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L420
 [NO\_EXPORT\_DYNAMIC\_SYMBOLS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1331
 [NO\_EXTENDED\_SOURCE\_SEARCH]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L335
 [NO\_IMPORT\_TRACING]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1085
 [NO\_IWYU]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4509
 [NO\_JOIN\_SRC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4446
 [NO\_LIBC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4400
 [NO\_LINT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1781
 [NO\_LTO]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L402
 [NO\_MYPY]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L510
 [NO\_NEED\_CHECK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4575
 [NO\_OPTIMIZE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4338
 [NO\_OPTIMIZE\_PY\_PROTOS]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L164
 [NO\_PLATFORM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4411
 [NO\_PROFILE\_RUNTIME]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4490
 [NO\_PYTHON\_COVERAGE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1077
 [NO\_RUNTIME]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4389
 [NO\_SANITIZE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4454
 [NO\_SANITIZE\_COVERAGE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4466
 [NO\_SPLIT\_DWARF]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2752
 [NO\_SSE4]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3164
 [NO\_TS\_TYPECHECK]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L300
 [NO\_UTIL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4378
 [NO\_WSHADOW]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4354
 [NO\_YMAKE\_PYTHON3]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L269
 [NVCC\_DEVICE\_LINK]: https://a.yandex-team.ru/arcadia/build/conf/cuda.conf?rev=20020720#L143
 [OBJC\_FLAGS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4325
 [ONLY\_TAGS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [OPENSOURCE\_EXPORT\_REPLACEMENT]: https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L83
 [OPENSOURCE\_EXPORT\_REPLACEMENT\_BY\_OS]: https://a.yandex-team.ru/arcadia/build/conf/opensource.conf?rev=20020720#L92
 [ORIGINAL\_SOURCE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5679
 [PACK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2480
 [PARALLEL\_TESTS\_WITHIN\_NODE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2939
 [PARTITIONED\_RECURSE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [PARTITIONED\_RECURSE\_FOR\_TESTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [PARTITIONED\_RECURSE\_ROOT\_RELATIVE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [PEERDIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [PIRE\_INLINE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4152
 [PIRE\_INLINE\_CMD]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4147
 [POPULATE\_CPP\_COVERAGE\_FLAGS]: https://a.yandex-team.ru/arcadia/build/conf/coverage_full_instrumentation.conf?rev=20020720#L7
 [POPULATE\_CPP\_YNDEXING]: https://a.yandex-team.ru/arcadia/build/conf/yndexing/cpp_instrumentation.conf?rev=20020720#L6
 [PREPARE\_INDUCED\_DEPS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4677
 [PROCESSOR\_CLASSES]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L118
 [PROCESS\_DOCS]: https://a.yandex-team.ru/arcadia/build/plugins/docs.py?rev=20020720#L41
 [PROCESS\_MKDOCS]: https://a.yandex-team.ru/arcadia/build/internal/plugins/mkdocs.py?rev=20020720#L38
 [PROTO2FBS]: https://a.yandex-team.ru/arcadia/build/conf/fbs.conf?rev=20020720#L162
 [PROTOC\_FATAL\_WARNINGS]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L144
 [PROTO\_ADDINCL]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L117
 [PROTO\_CMD]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L1077
 [PROTO\_NAMESPACE]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L134
 [PROTO\_TO\_NAMESPACE]: https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_proto.py?rev=20020720#L11
 [PROVIDES]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [PYTHON2\_ADDINCL]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L944
 [PYTHON2\_MODULE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L631
 [PYTHON3\_ADDINCL]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L993
 [PYTHON3\_MODULE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L643
 [PYTHON\_PATH]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1761
 [PY\_CONSTRUCTOR]: https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L790
 [PY\_DOCTESTS]: https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L701
 [PY\_ENUMS\_SERIALIZATION]: https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L806
 [PY\_EXTRALIBS]: https://a.yandex-team.ru/arcadia/build/plugins/extralibs.py?rev=20020720#L4
 [PY\_EXTRA\_LINT\_FILES]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1199
 [PY\_MAIN]: https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L767
 [PY\_NAMESPACE]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L680
 [PY\_PROTOS\_FOR]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [PY\_PROTO\_PLUGIN]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L189
 [PY\_PROTO\_PLUGIN2]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L201
 [PY\_REGISTER]: https://a.yandex-team.ru/arcadia/build/plugins/pybuild.py?rev=20020720#L720
 [PY\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1125
 [RECURSE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [RECURSE\_FOR\_TESTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [RECURSE\_ROOT\_RELATIVE]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [REGISTER\_SANDBOX\_IMPORT]: https://a.yandex-team.ru/arcadia/build/internal/plugins/sandbox_registry.py?rev=20020720#L6
 [REGISTER\_YQL\_PYTHON\_UDF]: https://a.yandex-team.ru/arcadia/build/plugins/yql_python_udf.py?rev=20020720#L4
 [REQUIREMENTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1677
 [REQUIRES]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L465
 [REQUIRE\_RESOURCE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1629
 [RESOLVE\_PROTO]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L661
 [RESOURCE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L524
 [RESOURCE\_FILES]: https://a.yandex-team.ru/arcadia/build/plugins/res.py?rev=20020720#L12
 [RESTRICT\_PATH]: https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L14
 [RISK\_GEN\_DATA\_MODEL]: https://a.yandex-team.ru/arcadia/build/internal/plugins/fintech_risk_model.py?rev=20020720#L276
 [ROS\_SRCS]: https://a.yandex-team.ru/arcadia/build/internal/plugins/ros.py?rev=20020720#L5
 [RUN]: https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L995
 [RUN\_ANTLR]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5247
 [RUN\_ANTLR4]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5267
 [RUN\_ANTLR4\_CPP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5291
 [RUN\_ANTLR4\_CPP\_SPLIT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5281
 [RUN\_ANTLR4\_GO]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5301
 [RUN\_ANTLR4\_PYTHON2]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5314
 [RUN\_ANTLR4\_PYTHON3]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5327
 [RUN\_JAVASCRIPT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L267
 [RUN\_JAVASCRIPT\_AFTER\_BUILD]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L252
 [RUN\_JAVA\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L632
 [RUN\_LUA]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4807
 [RUN\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4781
 [RUN\_PY3\_PROGRAM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4861
 [RUN\_PYTHON3]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4833
 [SDBUS\_CPP\_ADAPTOR]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5648
 [SDBUS\_CPP\_PROXY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5654
 [SDC\_DIAGS\_SPLIT\_GENERATOR\_V3]: https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_diagnostics.py?rev=20020720#L61
 [SDC\_DIAGS\_SPLIT\_GENERATOR\_V4]: https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc_diagnostics.py?rev=20020720#L24
 [SDC\_INSTALL]: https://a.yandex-team.ru/arcadia/build/internal/plugins/sdc.py?rev=20020720#L59
 [SELECT\_CLANG\_SA\_CONFIG]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L167
 [SELECT\_PROTO\_LAYOUT]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L82
 [SET]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SETUP\_EXECTEST]: https://a.yandex-team.ru/arcadia/build/plugins/_dart_fields.py?rev=20020720#L61
 [SETUP\_PYTEST\_BIN]: https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L987
 [SETUP\_RUN\_PYTHON]: https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L1041
 [SET\_APPEND]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SET\_APPEND\_WITH\_GLOBAL]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SET\_COMPILE\_OUTPUTS\_MODIFIERS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3192
 [SET\_CPP\_COVERAGE\_FLAGS]: https://a.yandex-team.ru/arcadia/build/plugins/coverage.py?rev=20020720#L43
 [SET\_CUSTOM\_CLANG\_TIDY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1189
 [SET\_RESOURCE\_MAP\_FROM\_JSON]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SET\_RESOURCE\_URI\_FROM\_JSON]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SIZE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3020
 [SKIP\_TEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1770
 [SOURCE\_GROUP]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SPLIT\_CODEGEN]: https://a.yandex-team.ru/arcadia/build/internal/plugins/split_codegen.py?rev=20020720#L10
 [SPLIT\_DWARF]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2744
 [SPLIT\_FACTOR]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2984
 [SRC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3779
 [SRCDIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/yndex/builtin.cpp?rev=20020720#L16
 [SRCS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3798
 [SRC\_C\_AMX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3933
 [SRC\_C\_AVX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3909
 [SRC\_C\_AVX2]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3917
 [SRC\_C\_AVX512]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3925
 [SRC\_C\_NO\_LTO]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4007
 [SRC\_C\_PIC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3999
 [SRC\_C\_SSE2]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3869
 [SRC\_C\_SSE3]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3877
 [SRC\_C\_SSE4]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3893
 [SRC\_C\_SSE41]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3901
 [SRC\_C\_SSSE3]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3885
 [SRC\_C\_XOP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3942
 [SRC\_RESOURCE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L739
 [STRIP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4332
 [STYLE\_CPP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5740
 [STYLE\_DETEKT]: https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L52
 [STYLE\_DUMMY]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L376
 [STYLE\_FLAKE8]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L404
 [STYLE\_JSON]: https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L13
 [STYLE\_PY2\_FLAKE8]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L412
 [STYLE\_PYTHON]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L384
 [STYLE\_RUFF]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L395
 [STYLE\_YAML]: https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L23
 [STYLE\_YQL]: https://a.yandex-team.ru/arcadia/build/conf/custom_lint.conf?rev=20020720#L33
 [SUBSCRIBER]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4598
 [SUPPRESSIONS]: https://a.yandex-team.ru/arcadia/build/plugins/suppressions.py?rev=20020720#L4
 [SYMLINK]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4649
 [SYSTEM\_PROPERTIES]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1815
 [TAG]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1666
 [TASKLET]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5346
 [TASKLET\_REG]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5363
 [TASKLET\_REG\_EXT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5378
 [TEST\_CWD]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2624
 [TEST\_DATA]: https://a.yandex-team.ru/arcadia/build/plugins/ytest.py?rev=20020720#L118
 [TEST\_JAVA\_CLASSPATH\_CMD\_TYPE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2370
 [TEST\_SRCS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1598
 [THINLTO\_CACHE]: https://a.yandex-team.ru/arcadia/build/conf/linkers/ld.conf?rev=20020720#L434
 [TIMEOUT]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2912
 [TOOLCHAIN]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5798
 [TS\_BIOME]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L370
 [TS\_BUILD\_ENV]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L276
 [TS\_BUILD\_OUTPUTS]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L66
 [TS\_BUILD\_SCRIPT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_library.conf?rev=20020720#L62
 [TS\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L36
 [TS\_ESLINT\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L143
 [TS\_EXCLUDE\_FILES\_GLOB]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L101
 [TS\_FILES]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L219
 [TS\_FILES\_GLOB]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L228
 [TS\_LARGE\_FILES]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L242
 [TS\_LINT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L8
 [TS\_NEXT\_BUILD\_OPTIONS]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L22
 [TS\_NEXT\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L11
 [TS\_NEXT\_EXPERIMENTAL\_BUILD\_MODE]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L45
 [TS\_NEXT\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_next.conf?rev=20020720#L35
 [TS\_PROTO\_OPT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_proto.conf?rev=20020720#L88
 [TS\_PROTO\_PACKAGE\_NAME]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_proto.conf?rev=20020720#L103
 [TS\_RSPACK\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L10
 [TS\_RSPACK\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_rspack.conf?rev=20020720#L22
 [TS\_STYLELINT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L344
 [TS\_TEST]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_check.conf?rev=20020720#L12
 [TS\_TEST\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L234
 [TS\_TEST\_DATA]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L275
 [TS\_TEST\_DEPENDS\_ON\_BUILD]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L284
 [TS\_TEST\_INCLUDE\_NODEJS]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L291
 [TS\_TEST\_SRCS]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L252
 [TS\_TYPECHECK]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_test.conf?rev=20020720#L321
 [TS\_USE\_BUN]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts.conf?rev=20020720#L283
 [TS\_VITE\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L10
 [TS\_VITE\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L24
 [TS\_WEBPACK\_CONFIG]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L10
 [TS\_WEBPACK\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L22
 [UBERJAR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1873
 [UBERJAR\_APPENDING\_TRANSFORMER]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1980
 [UBERJAR\_HIDE\_EXCLUDE\_PATTERN]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1909
 [UBERJAR\_HIDE\_INCLUDE\_PATTERN]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1924
 [UBERJAR\_HIDING\_PREFIX]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1894
 [UBERJAR\_MANIFEST\_TRANSFORMER\_ATTRIBUTE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1964
 [UBERJAR\_MANIFEST\_TRANSFORMER\_MAIN]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1951
 [UBERJAR\_PATH\_EXCLUDE\_PREFIX]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1938
 [UBERJAR\_SERVICES\_RESOURCE\_TRANSFORMER]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1993
 [UDF\_NO\_PROBE]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L26
 [UDF\_NO\_SCAN]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L33
 [UPDATE\_VCS\_JAVA\_INFO\_NODEP]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4203
 [USE\_ANNOTATION\_PROCESSOR]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L661
 [USE\_COMMON\_GOOGLE\_APIS]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L363
 [USE\_CXX]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4421
 [USE\_DYNAMIC\_CUDA]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1346
 [USE\_ERROR\_PRONE]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L1847
 [USE\_JAVALITE]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L537
 [USE\_KTLINT\_OLD]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2486
 [USE\_LEGACY\_PNPM\_VIRTUAL\_STORE]: https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L80
 [USE\_LINKER\_GOLD]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L883
 [USE\_LLVM\_BC16]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4953
 [USE\_LLVM\_BC18]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4958
 [USE\_LLVM\_BC20]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4963
 [USE\_MODERN\_FLEX]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L112
 [USE\_MODERN\_FLEX\_WITH\_HEADER]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L123
 [USE\_NASM]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4438
 [USE\_OLD\_FLEX]: https://a.yandex-team.ru/arcadia/build/conf/bison_lex.conf?rev=20020720#L132
 [USE\_PERSISTENT\_RECIPE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1747
 [USE\_PLANTUML]: https://a.yandex-team.ru/arcadia/build/conf/docs.conf?rev=20020720#L275
 [USE\_PYTHON2]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1046
 [USE\_PYTHON3]: https://a.yandex-team.ru/arcadia/build/conf/python.conf?rev=20020720#L1063
 [USE\_RECIPE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1730
 [USE\_SA\_PLUGINS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L176
 [USE\_SKIFF]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L305
 [USE\_UTIL]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4429
 [USRV\_GEN\_GRPC\_CLIENT\_V2]: https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L338
 [USRV\_GEN\_GRPC\_CLIENT\_V2\_STRUCTS]: https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L343
 [USRV\_GEN\_GRPC\_SERVICE\_V2]: https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L348
 [USRV\_GEN\_GRPC\_SERVICE\_V2\_STRUCTS]: https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L353
 [USRV\_GEN\_PROTO\_STRUCTS]: https://a.yandex-team.ru/arcadia/build/internal/plugins/userver.py?rev=20020720#L318
 [VALIDATE\_DATA\_RESTART]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L2922
 [VALIDATE\_IN\_DIRS]: https://a.yandex-team.ru/arcadia/build/plugins/macros_with_error.py?rev=20020720#L38
 [VCS\_INFO\_FILE]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4217
 [VERSION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L4606
 [VISIBILITY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5626
 [VITE\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_vite.conf?rev=20020720#L30
 [WEBPACK\_OUTPUT]: https://a.yandex-team.ru/arcadia/build/conf/ts/ts_webpack.conf?rev=20020720#L28
 [WINDOWS\_LONG\_PATH\_MANIFEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5611
 [WINDOWS\_MANIFEST]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5606
 [WITHOUT\_LICENSE\_TEXTS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5700
 [WITHOUT\_VERSION]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5802
 [WITH\_DYNAMIC\_LIBS]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1076
 [WITH\_JDK]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2188
 [WITH\_KAPT]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2219
 [WITH\_KOTLIN]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2207
 [WITH\_KOTLINC\_ALLOPEN]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2251
 [WITH\_KOTLINC\_DETEKT]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2297
 [WITH\_KOTLINC\_LOMBOK]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2262
 [WITH\_KOTLINC\_NOARG]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2274
 [WITH\_KOTLINC\_SERIALIZATION]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2286
 [WITH\_KOTLIN\_GRPC]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L231
 [WITH\_NODE\_MODULES]: https://a.yandex-team.ru/arcadia/build/conf/ts/node_modules.conf?rev=20020720#L27
 [WITH\_YA\_1931]: https://a.yandex-team.ru/arcadia/build/conf/java.conf?rev=20020720#L2514
 [YABS\_GENERATE\_CONF]: https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L11
 [YABS\_GENERATE\_PHANTOM\_CONF\_PATCH]: https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L43
 [YABS\_GENERATE\_PHANTOM\_CONF\_TEST\_CHECK]: https://a.yandex-team.ru/arcadia/build/internal/plugins/yabs_generate_conf.py?rev=20020720#L54
 [YA\_CONF\_JSON]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L5721
 [YDL\_DESC\_USE\_BINARY]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L3770
 [YQL\_ABI\_VERSION]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L249
 [YQL\_LAST\_ABI\_VERSION]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yql_udf.conf?rev=20020720#L258
 [YT\_ORM\_PROTO\_YSON]: https://a.yandex-team.ru/arcadia/build/conf/proto.conf?rev=20020720#L446
 [YT\_RECORD\_DISABLE\_PEERDIR]: https://a.yandex-team.ru/arcadia/build/conf/project_specific/yt.conf?rev=20020720#L3
 [YT\_SPEC]: https://a.yandex-team.ru/arcadia/build/ymake.core.conf?rev=20020720#L1587
 [ALIASES]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L31
 [ALLOWED]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L34
 [ALLOWED\_IN\_LINTERS\_MAKE]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L35
 [ARGS\_PARSER]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L43
 [CMD]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L44
 [DEFAULT\_NAME\_GENERATOR]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L45
 [EPILOGUE]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L46
 [EXTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L47
 [FILE\_GROUP]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L91
 [FINAL\_TARGET]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L50
 [GEN\_FROM\_FILE]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L53
 [GLOBAL]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L56
 [GLOBAL\_CMD]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L57
 [GLOBAL\_EXTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L60
 [GLOBAL\_SEM]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L61
 [IGNORED]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L64
 [INCLUDE\_TAG]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L67
 [NODE\_TYPE]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L70
 [NO\_EXPAND]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L73
 [PEERDIRSELF]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L80
 [PEERDIR\_POLICY]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L77
 [PROXY]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L81
 [RESTRICTED]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L87
 [SEM]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L88
 [SYMLINK\_POLICY]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L89
 [TRANSITION]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L94
 [USE\_PEERS\_LATE\_OUTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L90
 [VERSION\_PROXY]: https://a.yandex-team.ru/arcadia/devtools/ymake/lang/properties.h?rev=20020720#L84
 [APPLIED\_EXCLUDES]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L23
 [ARCADIA\_BUILD\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L24
 [ARCADIA\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L25
 [AUTO\_INPUT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L26
 [BINDIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L27
 [CHECK\_INTERNAL]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L28
 [CMAKE\_CURRENT\_BINARY\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L29
 [CMAKE\_CURRENT\_SOURCE\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L30
 [CONSUME\_NON\_MANAGEABLE\_PEERS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L31
 [CURDIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L32
 [DART\_CLASSPATH]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L34
 [DART\_CLASSPATH\_DEPS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L33
 [DEFAULT\_MODULE\_LICENSE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L35
 [DEPENDENCY\_MANAGEMENT\_TAGS\_EXCLUDE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L37
 [DEPENDENCY\_MANAGEMENT\_TRANSPARENT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L38
 [DEPENDENCY\_MANAGEMENT\_VALUE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L36
 [DONT\_RESOLVE\_INCLUDES]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L39
 [DYNAMIC\_LINK]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L40
 [EV\_HEADER\_EXTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L41
 [EXCLUDE\_SUBMODULES]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L42
 [EXCLUDE\_VALUE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L43
 [EXPORTED\_BUILD\_SYSTEM\_BUILD\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L44
 [EXPORTED\_BUILD\_SYSTEM\_SOURCE\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L45
 [GLOBAL\_SUFFIX]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L47
 [GLOBAL\_TARGET]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L48
 [GO\_HAS\_INTERNAL\_TESTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L49
 [GO\_TEST\_FOR\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L50
 [HAS\_MANAGEABLE\_PEERS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L51
 [IGNORE\_JAVA\_DEPENDENCIES\_CONFIGURATION]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L52
 [INPUT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L53
 [INTERNAL\_EXCEPTIONS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L54
 [JAVA\_DEPENDENCIES\_CONFIGURATION\_VALUE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L55
 [MANAGED\_PEERS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L57
 [MANAGED\_PEERS\_CLOSURE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L56
 [MANGLED\_MODULE\_TYPE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L58
 [MODDIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L59
 [MODULE\_ARGS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L60
 [MODULE\_COMMON\_CONFIGS\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L61
 [MODULE\_KIND]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L62
 [MODULE\_LANG]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L63
 [MODULE\_PREFIX]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L64
 [MODULE\_SEM\_IGNORE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L70
 [MODULE\_SUFFIX]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L65
 [MODULE\_TYPE]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L69
 [NON\_NAMAGEABLE\_PEERS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L72
 [OUTPUT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L73
 [PASS\_PEERS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L76
 [PEERDIR\_TAGS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L77
 [PEERS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L79
 [PEERS\_LATE\_OUTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L78
 [PROTO\_HEADER\_EXTS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L80
 [PYTHON\_BIN]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L81
 [REALPRJNAME]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L82
 [SONAME]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L83
 [SRCS\_GLOBAL]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L85
 [START\_TARGET]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L86
 [TARGET]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L87
 [TEST\_CASE\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L88
 [TEST\_OUT\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L89
 [TEST\_SOURCE\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L90
 [TEST\_WORK\_ROOT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L91
 [TOOLS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L92
 [TS\_CONFIG\_DECLARATION]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L94
 [TS\_CONFIG\_DECLARATION\_MAP]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L93
 [TS\_CONFIG\_DEDUCE\_OUT]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L95
 [TS\_CONFIG\_OUT\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L96
 [TS\_CONFIG\_PRESERVE\_JSX]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L97
 [TS\_CONFIG\_ROOT\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L98
 [TS\_CONFIG\_SOURCE\_MAP]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L99
 [UNITTEST\_DIR]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L100
 [UNITTEST\_MOD]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L101
 [USE\_ALL\_SRCS]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L102
 [USE\_GLOBAL\_CMD]: https://a.yandex-team.ru/arcadia/devtools/ymake/vardefs.h?rev=20020720#L103
