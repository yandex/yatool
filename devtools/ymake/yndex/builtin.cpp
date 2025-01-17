#include "builtin.h"

#include <devtools/ymake/builtin_macro_consts.h>
#include <devtools/ymake/lang/properties.h>
#include <devtools/ymake/vardefs.h>

using namespace NYndex;

namespace NYndex {
    struct TNameDocPair {
        TStringBuf Name;
        TString DocText;
    };

    static const ::TSourceLocation BuiltinDocLink = __LOCATION__;
    static const TNameDocPair BuiltinDefinitions[] = {
        { NMacro::PEERDIR,
            "@usage: " + TString{NMacro::PEERDIR} + "(dirs...)  # builtin\n\n"
                "Specify project dependencies\n"
                "Indicates that the project depends on all of the projects from the list of dirs.\n"
                "Libraries from these directories will be collected and linked to the current target if the target is executable or sharedlib/dll.\n"
                "If the current target is a static library, the specified directories will not be built, but they will be linked to any executable target that will link the current library.\n"
            "@params:\n"
                "1. As arguments PEERDIR you can only use the LIBRARY directory (the directory with the PROGRAM/DLL and derived from them are prohibited to use as arguments PEERDIR).\n"
                "2. ADDINCL Keyword ADDINCL (written before the specified directory), adds the flag -I<path to library> the flags to compile the source code of the current project.\n"
                "Perhaps it may be removed in the future (in favor of a dedicated ADDINCL)" },
        { "DEPENDS",
            "@usage: DEPENDS(path1 [path2...]) # builtin\n\n"
                "Buildable targets that should be brought to the test run. This dependency is"
                "only used when tests run is requested. It will build the specified modules and"
                "bring them to the working directory of the test (in their Arcadia paths). It"
                "is reasonable to specify only final targets her (like programs, DLLs or"
                "packages). DEPENDS to UNION is the only exception: UNIONs are"
                "transitively closed at DEPENDS bringing all dependencies to the test.\n\n"

                "DEPENDS on multimodule will select and bring single final target. If more none"
                "or more than one final target available in multimodule DEPENDS to it will"
                "produce configuration error."
        },
        { NMacro::RECURSE,
            "@usage: " + TString{NMacro::RECURSE} + "(dirs...)  # builtin\n\n"
                "Add directories to the build\n"
                "All projects must be reachable from the root chain RECURSE() for monorepo continuous integration functionality" },
        { NMacro::PARTITIONED_RECURSE,
            "@usage: " + TString{NMacro::PARTITIONED_RECURSE} + "([BALANCING_CONFIG config] dirs...)  # builtin\n\n"
                "Add directories to the build\n"
                "All projects must be reachable from the root chain RECURSE() for monorepo continuous integration functionality.\n"
                "Arguments are processed in chunks"},
        { NMacro::RECURSE_FOR_TESTS,
            "@usage: " + TString{NMacro::RECURSE_FOR_TESTS} + "(dirs...)  # builtin\n\n"
                "Add directories to the build if tests are demanded.\n"
                "Use --force-build-depends flag if you want to build testing modules without tests running"},
        { NMacro::PARTITIONED_RECURSE_FOR_TESTS,
            "@usage: " + TString{NMacro::PARTITIONED_RECURSE_FOR_TESTS} + "([BALANCING_CONFIG config] dirs...)  # builtin\n\n"
                "Add directories to the build if tests are demanded.\n"
                "Arguments are processed in chunks"},
        { NMacro::ADDINCL,
            "@usage: " + TString{NMacro::ADDINCL} + "([FOR <lang>][GLOBAL dir]* dirlist)  # builtin\n\n"
                "The macro adds the directories to include/import search path to compilation flags of the current project.\n"
                "By default settings apply to C/C++ compilation namely sets -I<library path> flag, use FOR argument to change target command.\n"
            "@params:\n"
                "`FOR <lang>` - adds includes/import search path for other language. E.g. `FOR proto` adds import search path for .proto files processing.\n"
                "`GLOBAL` - extends the search for headers (-I) on the dependent projects." },
        { "DLL_FOR",
            "@usage: DLL_FOR(path/to/lib [libname] [major_ver [minor_ver]] [EXPORTS symlist_file])  #builtin\n\n"
                "DLL module definition based on specified LIBRARY" },
        { "SUBSCRIBER",
            "@usage: SUBSCRIBER(subscribers...)  # builtin\n\n"
                "Add observers of the code.\n"
                "In the SUBSCRIBER macro you can use:\n"
                "1. login-s from staff.yandex-team.ru\n"
                "2. Review group (to specify the Code-review group need to use the prefix g:)\n\n"
                "Note: current behavior of SUBSCRIBER is almost the same as OWNER. The are only 2 differences: "
                "SUBSCRIBER is not mandatory and it may be separately processed by external tools\n\n"
                "Ask devtools@yandex-team.ru if you need more information" },
        { NMacro::RECURSE_ROOT_RELATIVE,
            "@usage: "+ TString{NMacro::RECURSE_ROOT_RELATIVE} + "(dirlist)  # builtin\n\n"
                "In comparison with RECURSE(), in dirlist there must be a directory relative to the root (${ARCADIA_ROOT})" },
        { "PARTITIONED_RECURSE_ROOT_RELATIVE",
            "@usage: PARTITIONED_RECURSE_ROOT_RELATIVE([BALANCING_CONFIG config] dirlist)  # builtin\n\n"
                "In comparison with RECURSE(), in dirlist there must be a directory relative to the root (${ARCADIA_ROOT}).\n"
                "Arguments are processed in chunks"},
        { NMacro::SRCDIR,
            "@usage: " + TString{NMacro::SRCDIR} + "(dirlist)  # builtin\n\n"
                "Add the specified directories to the list of those in which the source files will be searched\n"
                "Available only for arcadia/contrib"},
        { "ONLY_TAGS",
            "@usage: ONLY_TAGS(tags...)  # builtin\n\n"
                "Instantiate from multimodule only variants with tags listed" },
        { "EXCLUDE_TAGS",
            "@usage: EXCLUDE_TAGS(tags...)  # builtin\n\n"
                "Instantiate from multimodule all variants except ones with tags listed" },
        { "INCLUDE_TAGS",
            "@usage: INCLUDE_TAGS(tags...)  # builtin\n\n"
                "Additionally instantiate from multimodule all variants with tags listed (overrides default)" },
        { "END",
            "@usage: END()  # builtin\n\n"
                "The end of the module" },
        { "MESSAGE",
            "@usage: MESSAGE([severity] message)  # builtin\n\n"
                "Print message with given severity level (STATUS, FATAL_ERROR)"},
        { "PY_PROTOS_FOR",
            "@usage: PY_PROTOS_FOR(path/to/module)  #builtin, deprecated\n\n"
                "Use PROTO_LIBRARY() in order to have .proto compiled into Python.\n"
                "Generates pb2.py files out of .proto files and saves those into PACKAGE module"},
        { "INDUCED_DEPS",
            "@usage: INDUCED_DEPS(Extension Path...)  #builtin\n\n"
                "States that files wih the Extension generated by the PROGRAM will depend on files in Path.\n"
                "This only useful in PROGRAM and similar modules. It will be applied if the PROGRAM is used in RUN_PROGRAM macro.\n"
                "All Paths specified must be absolute arcadia paths i.e. start with ${ARCADIA_ROOT} ${ARCADIA_BUILD_ROOT}, ${CURDIR} "
                "or ${BINDIR}.\n" },
        { "NO_BUILD_IF",
            "@usage: NO_BUILD_IF([FATAL_ERROR|STRICT] variables)  # builtin\n\n"
                "Print warning or error if some variable is true.\n"
                "In STRICT mode disables build of all modules and RECURSES of the ya.make.\n"
                "FATAL_ERROR issues configure error and enables STRICT mode." },
        { "BUILD_ONLY_IF",
            "@usage: BUILD_ONLY_IF([FATAL_ERROR|STRICT] variables)  # builtin\n\n"
                "Print warning if all variables are false. For example, BUILD_ONLY_IF(LINUX WIN32)\n"
                "In STRICT mode disables build of all modules and RECURSES of the ya.make.\n"
                "FATAL_ERROR issues configure error and enables STRICT mode" },
        { "GO_TEST_FOR",
            "@usage: GO_TEST_FOR(path/to/module)  #builtin\n\n"
            "Produces go test for specified module"},
        { "TS_TEST_FOR",
            "@usage: TS_TEST_FOR(path/to/module)  #builtin\n\n"
            "Produces typescript test for specified module"},
        { NMacro::IF,
            "@usage IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin\n\n"
                 "Apply macros if condition holds" },
        { NMacro::ELSEIF,
            "@usage IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin\n\n"
                 "Apply macros if other_condition holds while none of previous conditions hold" },
        { NMacro::ELSE,
            "@usage IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin\n\n"
                 "Apply macros if none of previous conditions hold" },
        { NMacro::ENDIF,
            "@usage IF(condition) .. ELSEIF(other_condition) .. ELSE() .. ENDIF()  #builtin\n\n"
                 "End of conditional construct" },
        { NMacro::SET,
            "@usage SET(varname value)  #builtin\n\n"
                "Sets varname to value"},
        { NMacro::SET_APPEND,
            "@usage SET_APPEND(varname appendvalue)  #builtin\n\n"
                 "Appends appendvalue to varname's value using space as a separator" },
        { NMacro::SET_APPEND_WITH_GLOBAL,
            "@usage SET_APPEND_WITH_GLOBAL(varname appendvalue)  #builtin\n\n"
                 "Appends appendvalue to varname's value using space as a separator.\n"
                 "New value is propagated to dependants"},
        { NMacro::DEFAULT,
            "@usage DEFAULT(varname value)  #builtin\n\n"
                "Sets varname to value if value is not set yet"},
        { NMacro::ENABLE,
            "@usage ENABLE(varname)  #builtin\n\n"
                "Sets varname to 'yes'"},
        { NMacro::DISABLE,
            "@usage DISABLE(varname)  #builtin\n\n"
                "Sets varname to 'no'"},
        { "DECLARE_EXTERNAL_RESOURCE",
            "@usage DECLARE_EXTERNAL_RESOURCE(name sbr:id name1 sbr:id1...)  #builtin\n\n"
                "Associate name with sbr-id.\n\n"

                "Ask devtools@yandex-team.ru if you need more information" },
        { "DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE",
            "@usage DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE(name sbr:id FOR platform1 sbr:id FOR platform2...)  #builtin\n\n"
                "Associate name with sbr-id on platform.\n\n"

                "Ask devtools@yandex-team.ru if you need more information" },
        { "DECLARE_EXTERNAL_HOST_RESOURCES_PACK",
            "@usage DECLARE_EXTERNAL_HOST_RESOURCES_PACK(RESOURCE_NAME name sbr:id FOR platform1 sbr:id FOR platform2... RESOURCE_NAME name1 sbr:id1 FOR platform1...)  #builtin\n\n"
                "Associate name with sbr-id on platform.\n\n"

                "Ask devtools@yandex-team.ru if you need more information" },

        { "INCLUDE",
            "@usage INCLUDE(filename)  #builtin\n\n"
                "Include file textually and process it as a part of the ya.make" },
        { "INCLUDE_ONCE",
            "@usage INCLUDE_ONCE([yes|no])  #builtin\n\n"
                "Control how file is is processed if it is included into one base ya.make by multiple paths.\n"
                "if `yes` passed or argument omitted, process it just once. Process each time if `no` is passed (current default)\n"
                "Note: for includes from multimodules the file is processed once from each submodule (like if INCLUDEs were preprocessed into multimodule body)"},
        { NMacro::_GLOB,
            "@usage _GLOB(varname globs...)  #builtin, internal\n\n"
                "Sets varname to results of globs application.\n\n"
                ""
                "In globs are supported:\n"
                "1. Globstar option: '**' recursively matches directories (allowed once per glob).\n"
                "2. Wildcard patterns for files and directories with '?' and '*' special characters.\n\n"
                ""
                "Note: globs may slow down builds and may match to garbage if extra files present in working copy. Use with care and only if ultimately needed."},
       { "EXTERNAL_RESOURCE",
            "@usage EXTERNAL_RESOURCE(...)  #builtin, deprecated\n\n"
                "Don't use this. Use RESOURCE_LIBRARY or FROM_SANDBOX instead"},
        { "EXTRADIR", "@usage EXTRADIR(...)  #builtin, deprecated\n\n"
                "Ignored"},
        { "SOURCE_GROUP",  "@usage SOURCE_GROUP(...)  #builtin, deprecated\n\n"
                "Ignored"},
        { "PROVIDES", "@usage: PROVIDES(Name...)\n\n"
                ""
                "Specifies provided features. The names must be correct C identifiers.\n"
                "This prevents different libraries providing the same features to be linked into one program."},
        { "SET_RESOURCE_URI_FROM_JSON", "@usage: SET_RESOURCE_URI_FROM_JSON(VarName, FileName)\n\n"
                ""
                "Assigns a resource uri matched with a current target platform to the variable VarName.\n"
                "The 'platform to resource uri' mapping is loaded from json file 'FileName'. File content example:\n"
                "{\n"
                "    \"by_platform\": {\n"
                "        \"linux\": {\n"
                "            \"uri\": \"sbr:12345\"\n"
                "        },\n"
                "        \"darwin\": {\n"
                "            \"uri\": \"sbr:54321\"\n"
                "        }\n"
                "    }\n"
                "}\n"},
        { "SET_RESOURCE_MAP_FROM_JSON", "@usage: SET_RESOURCE_MAP_FROM_JSON(VarName, FileName)\n\n"
                ""
                "Loads the platform to resource uri mapping from the json file FileName and assign it to the variable VarName.\n"
                "'VarName' value format is the same as an input of the DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE macro and can be passed to this macro as is.\n"
                "File 'FileName' contains json with a 'canonized platform -> resource uri' mapping.\n"
                "The mapping file format see in SET_RESOURCE_URI_FROM_JSON description."},
        { "DECLARE_EXTERNAL_RESOURCE_BY_JSON", "@usage: DECLARE_EXTERNAL_RESOURCE_BY_JSON(VarName, FileName [, FriendlyResourceName])\n\n"
                ""
                "Associate 'Name' with a resource for the current target platform\n"
                "File 'FileName' contains json with a 'canonized platform -> resource uri' mapping.\n"
                "'FriendlyResourceName', if specified, is used in configuration error messages instead of VarName.\n"
                "The mapping file format see in SET_RESOURCE_URI_FROM_JSON description."},
        { "DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON", "@usage: DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(VarName, FileName [, FriendlyResourceName])\n\n"
                ""
                "Associate 'Name' with a platform to resource uri mapping\n"
                "File 'FileName' contains json with a 'canonized platform -> resource uri' mapping.\n"
                "'FriendlyResourceName', if specified, is used in configuration error messages instead of VarName.\n"
                "The mapping file format see in SET_RESOURCE_URI_FROM_JSON description."},
    };
}

void NYndex::AddBuiltinDefinitions(TDefinitions& definitions) {
    NYndex::TSourceRange range = {static_cast<size_t>(BuiltinDocLink.Line) + 1, 0, static_cast<size_t>(BuiltinDocLink.Line) + 1, 0};
    for (const auto& def : BuiltinDefinitions) {
        definitions.AddDefinition(TString{def.Name}, TString(BuiltinDocLink.File), range, def.DocText, NYndex::EDefinitionType::Macro);
    }
    for (const auto& option : NProperties::ALL_PROPERTIES) {
        NYndex::TSourceRange range = {static_cast<size_t>(option.Link.Line), 0, static_cast<size_t>(option.Link.Line), 0};
        definitions.AddDefinition(TString{option}, TString(option.Link.File), range, TString{option.Description}, NYndex::EDefinitionType::Property);
    }
    for (const auto& var : NVariableDefs::ALL_VARIABLES) {
        NYndex::TSourceRange range = {static_cast<size_t>(var.Link.Line), 0, static_cast<size_t>(var.Link.Line), 0};
        definitions.AddDefinition(TString{var}, TString(var.Link.File), range, TString{var.Description}, NYndex::EDefinitionType::Variable);
    }
}
