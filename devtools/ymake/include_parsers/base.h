#pragma once

#include "includes_parser_type.h"

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

class IContentHolder;

class TIncludesParserBase {
protected:
    TString IncPrefix;
    TString CommentSign;

public:
    TIncludesParserBase() = default;
    TIncludesParserBase(const TString& includePrefix, const TString& commentMarker);
    virtual ~TIncludesParserBase() = default;

protected:
    bool ParseIncludeLineBase(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile, const TString& incPrefix, const TString& commentSign) const;
    virtual bool ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) const;
    virtual bool IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const;
    virtual bool ParseCppIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) const;
};

void ChopIncludeComment(TStringBuf& lineBuf, const TStringBuf& commentSign);
bool NeedParseLine(size_t line);
