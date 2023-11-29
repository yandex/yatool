#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

class TNlgIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TInclDep>& imports);
};
