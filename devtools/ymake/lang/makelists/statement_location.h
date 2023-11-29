#pragma once

#include <util/generic/string.h>

struct TStatementLocation {
public:
    TString Path;
    size_t Row = 0;
    size_t Column = 0;
    size_t Pos = 0;

    TStatementLocation() = default;

    TStatementLocation(const TString& path, size_t row, size_t column, size_t pos)
        : Path(path)
        , Row(row)
        , Column(column)
        , Pos(pos)
    {
    }

    TStatementLocation& operator=(const TStatementLocation&) = default;
    TStatementLocation(const TStatementLocation&) = default;
};
