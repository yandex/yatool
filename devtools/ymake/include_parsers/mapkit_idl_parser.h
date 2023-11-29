#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>

class IContentHolder;

class TMapkitIdlIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes);
};
