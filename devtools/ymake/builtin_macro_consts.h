#pragma once

#include <util/generic/strbuf.h>

namespace NMacro {

constexpr TStringBuf VERSION = "VERSION";
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
constexpr TStringBuf PEERDIR = "PEERDIR";
constexpr TStringBuf SRCDIR = "SRCDIR";
constexpr TStringBuf ADDINCL = "ADDINCL";
constexpr TStringBuf _GHOST_PEERDIR = "_GHOST_PEERDIR";
constexpr TStringBuf _DATA_FILES = "_DATA_FILES";
constexpr TStringBuf _FOR_TESTS = "_FOR_TESTS";
constexpr TStringBuf RECURSE = "RECURSE";
constexpr TStringBuf RECURSE_ROOT_RELATIVE = "RECURSE_ROOT_RELATIVE";
constexpr TStringBuf RECURSE_FOR_TESTS = "RECURSE_FOR_TESTS";
constexpr TStringBuf PARTITIONED_RECURSE = "PARTITIONED_RECURSE";
constexpr TStringBuf PARTITIONED_RECURSE_FOR_TESTS = "PARTITIONED_RECURSE_FOR_TESTS";

}

namespace NArgs {

constexpr TStringBuf EXCLUDE = "EXCLUDE";
constexpr TStringBuf RESTRICTIONS = "RESTRICTIONS";
constexpr TStringBuf MAX_MATCHES = "MAX_MATCHES";
constexpr TStringBuf MAX_WATCH_DIRS = "MAX_WATCH_DIRS";

}
