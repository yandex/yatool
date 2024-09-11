#pragma once

#include "empty_parser.h"

class TCppLikeIncludesParser: public TIncludesParserBase {
public:
    TCppLikeIncludesParser();
    virtual void Parse(IContentHolder& file, TVector<TString>& includes) const;

protected:
    bool ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) const override;
    bool IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const override;
    void ScanIncludes(TVector<TString>& includes, IContentHolder& incFile) const;
};

class TCppOnlyIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes) const;
};
