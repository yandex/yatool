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
PROPERTY(ALLOWED_IN_LINTERS_MAKE, "")
PROPERTY(ALL_INS_TO_OUT, "")
PROPERTY(ARGS_PARSER, "")
PROPERTY(CMD, "")
PROPERTY(STRUCT_CMD, "Enables or disables new command template interpreter for this macro or module")
PROPERTY(STRUCT_SEM, "")
PROPERTY(DEFAULT_NAME_GENERATOR, "")
PROPERTY(EPILOGUE, "")
PROPERTY(EXTS, "@usage: `.EXTS=.o .obj` specify a list of extensions which are automatically captured as module AUTO_INPUT for all `output`s generated in the current module without the `noauto` modifier.")
PROPERTY(FINAL_TARGET, "")
PROPERTY(GEN_FROM_FILE, "")
PROPERTY(GLOBAL, "")
PROPERTY(GLOBAL_CMD, "")
PROPERTY(GLOBAL_EXTS, "")
PROPERTY(GLOBAL_SEM, "")
PROPERTY(IGNORED, "")
PROPERTY(INCLUDE_TAG, "")
PROPERTY(NODE_TYPE, "")
PROPERTY(NO_EXPAND, "")
PROPERTY(PEERDIR, "")
PROPERTY(PEERDIR_POLICY, "")
PROPERTY(PEERDIRSELF, "")
PROPERTY(PROXY, "")
PROPERTY(VERSION_PROXY, "")
PROPERTY(RESTRICTED, "")
PROPERTY(SEM, "")
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
