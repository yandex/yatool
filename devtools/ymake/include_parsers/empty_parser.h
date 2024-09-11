#pragma once

#include "base.h"

#include <devtools/ymake/symbols/symbols.h>

#include <util/generic/string.h>

class TEmptyIncludesParser {
public:
    void Parse(const IContentHolder&, TVector<TString>&) const;
};
