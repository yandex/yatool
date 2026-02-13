#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/array_ref.h>
#include <util/stream/output.h>

namespace NProperties {

struct TProperty {
    TStringBuf Name;
    TStringBuf Description;
    TSourceLocation Link;

    constexpr operator TStringBuf() const noexcept {
        return Name;
    }
};

static inline IOutputStream& operator<<(IOutputStream& o Y_LIFETIME_BOUND, const TProperty& opt) {
    o << opt.Name;
    return o;
}

#define PROPERTY(name, description) constexpr TProperty name{#name, description, __LOCATION__};

PROPERTY(ADDINCL, "@usage: .ADDINCL=[GLOBAL|LOCAL|ONE_LEVEL] [FOR lang] dir1 [dir2] ...\n\n"
""
"Adds include directories for the module, optionally scoped by language and propagation policy.")
PROPERTY(ALIASES, "@usage: .ALIASES=FROM1=TO1 [FROM2=TO2] ...\n\n"
""
"Defines macro name aliases for the module. When a macro FROM is used, it is redirected to macro TO before processing.")
PROPERTY(ALLOWED, "@usage: .ALLOWED=MACRO1 [MACRO2] ...\n\n"
""
"Restricts macros list allowed within the module.")
PROPERTY(ALLOWED_IN_LINTERS_MAKE, "@usage `.ALLOWED_IN_LINTERS_MAKE=yes` marks current macro as allowed for use in `linters.make.inc` files.")
PROPERTY(ARGS_PARSER, "Choose argument parser for macro opening curent module declaration. Must be one of: `Base`, `DLL` or `Raw`\n\n"
""
" * `Base` - Effective signature: `(Realprjname, PREFIX="")`. Value of the only positional parameter is stored in the REALPRJNAME variable.\n"
"            Value of the optional named parameter `PREFIX` is used to set MODULE_PREFIX variable.\n"
"            **Default** arg parser for module macros.\n"
" * `DLL` - Effective signature: `(Realprjname, PREFIX="", Ver...)`. First positional parameter and the only named parameter PREFIX are treated in the same way as in Base\n"
"           argument parser. Remaining positional parameters are treated as components of DLL so-version and are stored in a `MODULE_VERSION` variable in a joined by `.` string\n"
" * `Raw` - Do not perform any parsing or validation. Stores all arguments in a variable `MODULE_ARGS_RAW` which can be analyzed by macros invoked in the module body.\n")
PROPERTY(CMD, "Macro or module build command")
PROPERTY(DEFAULT_NAME_GENERATOR, "Name of embedded output filename generator, one of: UseDirNameOrSetGoPackage, TwoDirNames, ThreeDirNames, FullPath")
PROPERTY(EPILOGUE, "Name of a macro to invoke after the module body is fully parsed.")
PROPERTY(EXTS, "@usage: `.EXTS=.o .obj` specify a list of extensions which are automatically captured as module AUTO_INPUT for all `output`s generated in the current module without the `noauto` modifier.")
PROPERTY(FINAL_TARGET, "@usage: .FINAL_TARGET=yes|no\n\n"
""
"Marks the module as a final build target.")
PROPERTY(GEN_FROM_FILE, "@usage: .GEN_FROM_FILE=yes\n\n"
""
"Mark command as embedding configuration variables into files. Adds configuration variables in form of key=value to the end of .CMD.")
PROPERTY(GLOBAL, "@usage: .GLOBAL=VAR1 [VAR2] ...\n\n"
""
"Makes listed variables global. For each listed name a corresponding NAME_GLOBAL variable is created to collect values across dependent modules.")
PROPERTY(GLOBAL_CMD, "Build command for global sources (e.g. SRCS(GLOBAL ...)). Must be accompanied by .GLOBAL_EXTS.")
PROPERTY(GLOBAL_EXTS, "@usage: .GLOBAL_EXTS=.ext1 .ext2\n\n"
""
"Specify extensions which are treated as global inputs and processed by .GLOBAL_CMD.")
PROPERTY(GLOBAL_SEM, "Global semantics (instead of global commands) for export to other build systems in --sem-graph mode")
PROPERTY(IGNORED, "@usage: .IGNORED=MACRO1 [MACRO2] ...\n\n"
""
"Lists macros that are silently ignored within the module (neither processed nor causing an error).")
PROPERTY(INCLUDE_TAG, "@usage: .INCLUDE_TAG=yes|no\n\n"
""
"Controls whether a multimodule sub-module tag is included in the default set of active tags.")
PROPERTY(NODE_TYPE, "@usage: .NODE_TYPE=Library|Program|Bundle\n\n"
""
"Required. Sets the module node type in the build graph.")
PROPERTY(NO_EXPAND, "@usage: .NO_EXPAND=yes\n\n"
""
"Prevents expansion of the macro command variables during command evaluation.")
PROPERTY(PEERDIR, "Adds implicit PEERDIR dependencies to the module when the macro is invoked.")
PROPERTY(PEERDIR_POLICY, "@usage: .PEERDIR_POLICY=as_include|as_build_from\n\n"
""
"Controls how PEERDIRs to the module work. as_build_from makes dependants to just use results produced by the module; as_include makes dependants to include the module as a whole (with transitive info, for example).")
PROPERTY(PEERDIRSELF, "@usage: .PEERDIRSELF=TAG1 [TAG2] ...\n\n"
""
"Declares intra-multimodule dependencies: lists sub-module tags that the current sub-module depends on within the same multimodule.")
PROPERTY(PROXY, "")
PROPERTY(VERSION_PROXY, "@usage: `.VERSION_PROXY=yes` mark module as empty intermediate proxy for library with multiple versions.\n\n"
""
"Such module is always replaced by exact version of the library in dependency management phase of build configuration. It can only be used with dependency management aware modules.")
PROPERTY(RESTRICTED, "@usage: .RESTRICTED=MACRO1 [MACRO2] ...\n\n"
""
"Restricts listed macros from being used within the module. Complementary to .ALLOWED and .IGNORED properties.")
PROPERTY(SEM, "Semantics (instead of commands) for export to other build systems in --sem-graph mode")
PROPERTY(SYMLINK_POLICY, "")
PROPERTY(USE_PEERS_LATE_OUTS, "@usage `.USE_PEERS_LATE_OUTS=yes` enables propagation of dependencies `late_out`s from direct and transitive peers. Gathered late outs can be used by module command through late variable `PEERS_LATE_OUTS`.")
PROPERTY(FILE_GROUP, "__EXPERIMENTAL FEATUE__ allows to create complex group of files with graph representation similar to GLOB or ALL_SRCS. Not yet ready for production.")
PROPERTY(TRANSITION, "@usage: .TRANSITION=platform\n\n"
""
"Marks the module to be configured in foreign platform. Supported platforms now are pic, nopic.")

constexpr TProperty _PROPERTIES[]{
    ADDINCL,
    ALIASES,
    ALLOWED,
    ALLOWED_IN_LINTERS_MAKE,
    ARGS_PARSER,
    CMD,
    DEFAULT_NAME_GENERATOR,
    EPILOGUE,
    EXTS,
    FINAL_TARGET,
    GEN_FROM_FILE,
    GLOBAL,
    GLOBAL_CMD,
    GLOBAL_EXTS,
    GLOBAL_SEM,
    IGNORED,
    INCLUDE_TAG,
    NODE_TYPE,
    NO_EXPAND,
    PEERDIR,
    PEERDIR_POLICY,
    PEERDIRSELF,
    PROXY,
    VERSION_PROXY,
    RESTRICTED,
    SEM,
    SYMLINK_POLICY,
    USE_PEERS_LATE_OUTS,
    FILE_GROUP,
    TRANSITION,
};
constexpr TArrayRef<const TProperty> ALL_PROPERTIES{_PROPERTIES};
}  // namespace NProperties
