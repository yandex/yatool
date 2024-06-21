#pragma once

#include <devtools/ymake/common/content_holder.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

struct TRosDep {
    TString PackageName;
    TString MessageName;

    template <typename TStringType>
    TRosDep(TStringType&& packageName, TStringType&& messageName)
        : PackageName(std::move(packageName))
        , MessageName(std::move(messageName))
    {
    }

    bool operator==(const TRosDep& other) const {
        return other.PackageName == PackageName && other.MessageName == MessageName;
    }
};

class TRosIncludeParser {
public:
    void Parse(IContentHolder& file, TVector<TRosDep>& parsedIncludes);
};
