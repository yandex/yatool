#pragma once

#include <util/generic/string.h>

struct TInclDep {
    TString Path;
    bool IsInduced;

    template <typename TStringType>
    TInclDep(TStringType&& path, bool induced)
        : Path(std::forward<TStringType>(path))
        , IsInduced(induced)
    {
    }

    bool operator==(const TInclDep& other) const {
        return Path == other.Path && IsInduced == other.IsInduced;
    }
};
