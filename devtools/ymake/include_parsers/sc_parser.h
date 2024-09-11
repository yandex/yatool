#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

class TScIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes) const;
};
