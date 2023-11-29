#pragma once

#include "empty_parser.h"

class TCppLikeIncludesParser: public TIncludesParserBase {
public:
    TCppLikeIncludesParser();
    virtual void Parse(IContentHolder& file, TVector<TString>& includes);

protected:
    bool ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) override;
    bool IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) override;
    void ScanIncludes(TVector<TString>& includes, IContentHolder& incFile);
};

class TCppOnlyIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TString>& includes);
};
