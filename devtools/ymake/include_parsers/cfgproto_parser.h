#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

class TCfgprotoIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TInclDep>& includes) const;
};
