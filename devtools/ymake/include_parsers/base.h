#pragma once

#include <devtools/ymake/common/content_holder.h>

#include <util/generic/string.h>

class TIncludesParserBase {
protected:
    TString IncPrefix;
    TString CommentSign;

public:
    TIncludesParserBase() = default;
    TIncludesParserBase(const TString& includePrefix, const TString& commentMarker);
    virtual ~TIncludesParserBase() = default;

protected:
    bool ParseIncludeLineBase(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile, const TString& incPrefix, const TString& commentSign);
    virtual bool ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile);
    virtual bool IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix);
    virtual bool ParseCppIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile);
};

void ChopIncludeComment(TStringBuf& lineBuf, const TStringBuf& commentSign);
bool NeedParseLine(size_t line);
