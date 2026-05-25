#pragma once

#include <util/generic/string.h>

struct TStatementLocation {
    TString Path;
    size_t Row = 0;
    size_t Column = 0;
    size_t Pos = 0;
};
