#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

class TSwigIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TInclDep>& includes);
};
