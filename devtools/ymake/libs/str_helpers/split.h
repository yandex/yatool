#pragma once

#include <util/string/split.h>

inline auto SplitBySpace(TStringBuf value) {
    return StringSplitter(value).Split(' ').SkipEmpty();
}
