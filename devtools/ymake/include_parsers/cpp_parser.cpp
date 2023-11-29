#include "cpp_parser.h"
#include "cpp_includes_parser.h"

#include <util/string/strspn.h>

TCppLikeIncludesParser::TCppLikeIncludesParser()
    : TIncludesParserBase("#include", "//")
{
}

bool TCppLikeIncludesParser::IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) {
    return parts[0] == incPrefix || (parts.size() > 2 && parts[0] == TStringBuf("#") && parts[1] == TStringBuf("include"));
}

bool TCppLikeIncludesParser::ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) {
    return ParseIncludeLineBase(lineBuf, inc, incFile, IncPrefix, CommentSign);
}

void TCppLikeIncludesParser::ScanIncludes(TVector<TString>& includes, IContentHolder& incFile) {
    static TCompactStrSpn spn(" \t");

    size_t i = 0;
    TStringBuf lineBuf;

    TString inc;
    TStringBuf input = incFile.GetContent();

    while (input.ReadLine(lineBuf) && NeedParseLine(++i)) {
        const char* e = lineBuf.end();
        const char* p = spn.FindFirstNotOf(lineBuf.data(), e);

        if (p == e || *p != IncPrefix[0]) {
            continue;
        }

        lineBuf = TStringBuf(p, e);
        if (ParseNativeIncludeLine(lineBuf, inc, incFile)) {
            includes.push_back(inc);
        }
    }
}

void TCppLikeIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) {
    includes.reserve(64);
    ScanIncludes(includes, file);
}

void TCppOnlyIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) {
    TStringBuf input = file.GetContent();
    includes.reserve(64);
    ScanCppIncludes(input, includes);
}
