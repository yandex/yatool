#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

class TFlatcIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes);
};
