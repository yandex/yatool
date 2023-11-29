#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

class IMacroValueLookup {
public:
    virtual bool Get(const TStringBuf& macroName, const TStringBuf& key, TString& out) const = 0;
};

TString Expand(const IMacroValueLookup& lookup, const TStringBuf& expr);
