#include "asm_parser.h"

TAsmLikeIncludesParser::TAsmLikeIncludesParser() {
    IncPrefix = "%include";
    CommentSign = "//";
}

bool TAsmLikeIncludesParser::IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const {
    return TIncludesParserBase::IsPrefixMatches(parts, incPrefix);
}

bool TAsmIncludesParser::IsPrefixMatches(const TVector<TStringBuf>& parts, const TString& incPrefix) const {
    if (TIncludesParserBase::IsPrefixMatches(parts, incPrefix)) {
        return true;
    }

    TString upperIncPrefix = incPrefix;
    upperIncPrefix.to_upper(0);
    return TIncludesParserBase::IsPrefixMatches(parts, upperIncPrefix);
}
