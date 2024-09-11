#include "base.h"

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/options/static_options.h>

#include <util/string/split.h>

static const TString CPP_INCLUDE_PREFIX = "#include";
static const TString CPP_COMMENT_SIGN = "//";

bool NeedParseLine(size_t line) {
    return line <= NStaticConf::INCLUDE_LINES_LIMIT;
}

void ChopIncludeComment(TStringBuf& lineBuf, const TStringBuf& commentSign) {
    size_t comment;
    if ((comment = lineBuf.find(commentSign)) != TString::npos) {
        lineBuf = lineBuf.SubStr(0, comment);
    }
}

inline static bool GetValidInclude(TStringBuf& incStr) {
    if (incStr.back() == ';') //delete ; at the end
        incStr.Chop(1);
    if (incStr.size() < 3 || incStr[0] != '"' && incStr[0] != '\'' && incStr[0] != '<')
        return false;
    return incStr[0] == incStr.back() || incStr[0] == '<' && incStr.back() == '>';
}

TIncludesParserBase::TIncludesParserBase(const TString& includePrefix, const TString& commentMarker)
    : IncPrefix(includePrefix)
    , CommentSign(commentMarker)
{
}

bool TIncludesParserBase::ParseNativeIncludeLine(TStringBuf& /* lineBuf */, TString& /* inc */, IContentHolder& /* incFile */) const {
    return false;
}

bool TIncludesParserBase::ParseIncludeLineBase(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile, const TString& incPrefix, const TString& commentSign) const {
    ChopIncludeComment(lineBuf, commentSign);

    TVector<TStringBuf> parts;
    Split(lineBuf, " \t", parts);

    if (!IsPrefixMatches(parts, incPrefix)) {
        return false;
    }
    TStringBuf incStr;

    // case: #include <...> /* comment */
    if (parts.size() > 1 && GetValidInclude(parts[1])) {
        incStr = parts[1];
    } else if (parts.size() > 2 && GetValidInclude(parts[2])) {
        incStr = parts[2];
    } else {
        YConfWarn(Incl) << "file: " << incFile.GetAbsoluteName() << " has bad include statement: " << lineBuf << Endl;
        return false;
    }

    TStringBuf target = incStr.SubStr(1, incStr.size() - 2);
    AssertEx(target.size(), "empty include: " << incFile.GetAbsoluteName());

    inc = TString{target};
    return true;
}

bool TIncludesParserBase::ParseCppIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) const {
    return ParseIncludeLineBase(lineBuf, inc, incFile, CPP_INCLUDE_PREFIX, CPP_COMMENT_SIGN);
}

bool TIncludesParserBase::IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const {
    return parts[0] == incPrefix;
}
