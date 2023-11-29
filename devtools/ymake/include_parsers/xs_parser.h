#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

class TXsIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TInclDep>& includes);
};
