#pragma once

#include <devtools/ymake/symbols/symbols.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>


class TYDLIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes);
};
