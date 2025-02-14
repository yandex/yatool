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

PROPERTY(ADDINCL, "")
PROPERTY(ALIASES, "")
PROPERTY(ALLOWED, "@usage: .ALLOWED=MACRO1 [MACRO2] ...\n\n"
""
"Restricts macros list allowed within the module.")
PROPERTY(ALLOWED_IN_LINTERS_MAKE, "@usage `.ALLOWED_IN_LINTERS_MAKE=yes` marks current macro as allowed for use in `linters.make.inc` files.")
PROPERTY(ALL_INS_TO_OUT, "")
PROPERTY(ARGS_PARSER, "Choose argument parser for macro opening curent module declaration. Must be one of: `Base`, `DLL` or `Raw`\n\n"
""
" * `Base` - Effective signature: `(Realprjname, PREFIX="")`. Value of the only positional parameter is stored in the REALPRJNAME variable.\n"
"            Value of the optional named parameter `PREFIX` is used to set MODULE_PREFIX variable.\n"
"            **Default** arg parser for module macros.\n"
" * `DLL` - Effective signature: `(Realprjname, PREFIX="", Ver...)`. First positional parameter and the only named parameter PREFIX are treated in the same way as in Base\n"
"           argument parser. Remaining positional parameters are treated as components of DLL so-version and are stored in a `MODULE_VERSION` variable in a joined by `.` string\n"
" * `Raw` - Do not perform any parsing or validation. Stores all arguments in a variable `MODULE_ARGS_RAW` which can be analyzed by macros invoked in the module body.\n")
PROPERTY(CMD, "Macro or module build command")
PROPERTY(STRUCT_CMD, "Enables or disables new command template interpreter for this macro or module")
PROPERTY(STRUCT_SEM, "Enables or disables new semantics template interpreter for this macro or module")
PROPERTY(DEFAULT_NAME_GENERATOR, "Name of embedded output filename generator, one of: UseDirNameOrSetGoPackage, TwoDirNames, ThreeDirNames, FullPath")
PROPERTY(EPILOGUE, "")
PROPERTY(EXTS, "@usage: `.EXTS=.o .obj` specify a list of extensions which are automatically captured as module AUTO_INPUT for all `output`s generated in the current module without the `noauto` modifier.")
PROPERTY(FINAL_TARGET, "")
PROPERTY(GEN_FROM_FILE, "")
PROPERTY(GLOBAL, "")
PROPERTY(GLOBAL_CMD, "")
PROPERTY(GLOBAL_EXTS, "")
PROPERTY(GLOBAL_SEM, "Global semantics (instead of global commands) for export to other build systems in --sem-graph mode")
PROPERTY(IGNORED, "")
PROPERTY(INCLUDE_TAG, "")
PROPERTY(NODE_TYPE, "")
PROPERTY(NO_EXPAND, "")
PROPERTY(PEERDIR, "")
PROPERTY(PEERDIR_POLICY, "")
PROPERTY(PEERDIRSELF, "")
PROPERTY(PROXY, "")
PROPERTY(VERSION_PROXY, "@usage: `.VERSION_PROXY=yes` mark module as empty intermediate proxy for library with multiple versions.\n\n"
""
"Such module is always replaced by exact version of the library in dependency management phase of build configuration. It can only be used with dependency management aware modules.")
PROPERTY(RESTRICTED, "")
PROPERTY(SEM, "Semantics (instead of commands) for export to other build systems in --sem-graph mode")
PROPERTY(SYMLINK_POLICY, "")
PROPERTY(USE_INJECTED_DATA, "")
PROPERTY(USE_PEERS_LATE_OUTS, "")
PROPERTY(FILE_GROUP, "")
PROPERTY(TRANSITION, "@usage: .TRANSITION=platform\n\n"
""
"Marks the module to be configured in foreign platform. Supported platforms now are pic, nopic.")

constexpr TProperty _PROPERTIES[]{
    ADDINCL,
    ALIASES,
    ALLOWED,
    ALLOWED_IN_LINTERS_MAKE,
    ALL_INS_TO_OUT,
    ARGS_PARSER,
    CMD,
    STRUCT_CMD,
    STRUCT_SEM,
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
    USE_INJECTED_DATA,
    USE_PEERS_LATE_OUTS,
    FILE_GROUP,
    TRANSITION,
};
constexpr TArrayRef<const TProperty> ALL_PROPERTIES{_PROPERTIES};
}  // namespace NProperties
