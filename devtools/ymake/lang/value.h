#pragma once

#include <devtools/ymake/polexpr/ids.h>
#include <util/generic/cast.h>

#include <cstdint>

enum class EStorageType: int {
    Inline,
    Pool,
    Count
};
static_assert(ToUnderlying(EStorageType::Count) <= (1 << NPolexpr::TConstId::STORAGE_BITS));

enum class EDataType: uint8_t {
    //
    Void,
    Bool,
    Input,
    Output,
    CountInline,
    //
    String = CountInline,
    Tool,
    Glob,
    InputArray,
    OutputArray,
    LegacyLateGlob,
    StringArray,
    ToolArray,
    Result,
    Count
};
