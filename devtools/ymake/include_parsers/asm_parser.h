#pragma once

#include "cpp_parser.h"

class TAsmLikeIncludesParser: public TCppLikeIncludesParser {
public:
    TAsmLikeIncludesParser();

protected:
    bool IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const override;
};

class TAsmIncludesParser: public TAsmLikeIncludesParser {
protected:
    bool IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const override;
};
