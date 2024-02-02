#pragma once

#include <util/generic/strbuf.h>

namespace NMacro {

constexpr TStringBuf IF = "IF";
constexpr TStringBuf ELSE = "ELSE";
constexpr TStringBuf ELSEIF = "ELSEIF";
constexpr TStringBuf ENDIF = "ENDIF";
constexpr TStringBuf SET = "SET";
constexpr TStringBuf SET_APPEND = "SET_APPEND";
constexpr TStringBuf SET_APPEND_WITH_GLOBAL = "SET_APPEND_WITH_GLOBAL";
constexpr TStringBuf DEFAULT = "DEFAULT";
constexpr TStringBuf ENABLE = "ENABLE";
constexpr TStringBuf DISABLE = "DISABLE";
constexpr TStringBuf _GLOB = "_GLOB";
constexpr TStringBuf _LATE_GLOB = "_LATE_GLOB";
constexpr TStringBuf _NEVERCACHE = "_NEVERCACHE";

}

namespace NArgs {

constexpr TStringBuf EXCLUDE = "EXCLUDE";

}
