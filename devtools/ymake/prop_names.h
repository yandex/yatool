#pragma once

#include <util/generic/strbuf.h>

namespace NProps {

constexpr TStringBuf GLOB = "GLOB";
constexpr TStringBuf LATE_GLOB = "LATE_GLOB";
constexpr TStringBuf GLOB_HASH = "GLOB_HASH";
constexpr TStringBuf GLOB_EXCLUDE = "GLOB_EXCLUDE";
constexpr TStringBuf REFERENCED_BY = "REFERENCED_BY";
constexpr TStringBuf USED_RESERVED_VAR = "USED_RESERVED_VAR";
constexpr TStringBuf LATE_OUT = "LATE_OUT";
constexpr TStringBuf TEST_RECURSES = "TEST_RECURSES";
constexpr TStringBuf RECURSES = "RECURSES";
constexpr TStringBuf DEPENDS = "DEPENDS";
constexpr TStringBuf ALL_SRCS = "ALL_SRCS";
constexpr TStringBuf MULTIMODULE = TStringBuf("MULTIMODULE");
constexpr TStringBuf NEVERCACHE_PROP = TStringBuf("NEVER=CACHE");

}

