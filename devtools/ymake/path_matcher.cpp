#include "path_matcher.h"

#include <util/stream/output.h>
#include <util/string/ascii.h>
#include <util/generic/algorithm.h>

#include <contrib/libs/re2/re2/re2.h>

namespace {
    struct TRE2Matcher: public IPathMatcher {
        static inline re2::StringPiece ToSP(const TStringBuf& s) noexcept {
            return {s.data(), s.length()};
        }

        inline TRE2Matcher(TStringBuf regex)
            : Pattern(ToSP(regex))
        {
        }

        bool Match(TStringBuf data) const override {
            return re2::RE2::PartialMatch(ToSP(data), Pattern);
        }

        re2::RE2 Pattern;
    };

    struct TPrefixMatcher: public IPathMatcher {
        inline TPrefixMatcher(const TStringBuf& s)
            : Prefix(s)
        {
        }

        bool Match(TStringBuf data) const override {
            return data.StartsWith(Prefix);
        }

        const TString Prefix;
    };

    static inline bool IsRegularChar(char ch) noexcept {
        if (IsAsciiAlnum(ch)) {
            return true;
        }

        if (ch == '/' || ch == '.' || ch == '-' || ch == '_') {
            return true;
        }

        return false;
    }

    static inline bool IsRegularPath(TStringBuf path) noexcept {
        return AllOf(path.begin(), path.end(), IsRegularChar);
    }
}

IPathMatcher::TRef IPathMatcher::Construct(const TString& regex) {
    if (regex && regex[0] == '^') {
        const TStringBuf path = TStringBuf(regex).Skip(1);

        if (IsRegularPath(path)) {
            return new TPrefixMatcher(path);
        }
    }

    return new TRE2Matcher(regex);
}
