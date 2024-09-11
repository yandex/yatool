#pragma once

#include <devtools/ymake/common/content_holder.h>

#include <util/generic/string.h>

enum class EIncludesParserType : ui32 {
    EmptyParser = 0,
    CppOnlyParser,
    AsmParser,
    ProtoParser,
    LexParser,
    RagelParser,
    MapkitIdlParser,
    FortranParser,
    XsParser,
    XsynParser,
    SwigParser,
    CythonParser,
    FlatcParser,
    GoParser,
    ScParser,
    YDLParser,
    NlgParser,
    CfgprotoParser,
    TsParser,
    RosParser,
    PARSERS_COUNT,
    BAD_PARSER = PARSERS_COUNT
};

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
