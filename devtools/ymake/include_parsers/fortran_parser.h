#pragma once

#include <devtools/ymake/symbols/symbols.h>

class TFortranIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes);
};
