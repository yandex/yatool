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

    struct TExactMatcher: public IPathMatcher {
        explicit TExactMatcher(TStringBuf path)
            : Path(path)
        {
        }

        bool Match(TStringBuf data) const override {
            return data == Path;
        }

        const TString Path;
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

    bool IsLiteralPathChar(char ch) noexcept {
        return IsAsciiAlnum(ch) || ch == '/' || ch == '-' || ch == '_';
    }

    bool IsLiteralPath(TStringBuf path) noexcept {
        return AllOf(path.begin(), path.end(), IsLiteralPathChar);
    }

    bool IsRegularPath(TStringBuf path) noexcept {
        return AllOf(path.begin(), path.end(), [](char ch) {
            return IsLiteralPathChar(ch) || ch == '.';
        });
    }
}

IPathMatcher::TRef IPathMatcher::Construct(const TString& regex) {
    if (regex && regex[0] == '^') {
        const TStringBuf path = TStringBuf(regex).Skip(1);

        TStringBuf literal;
        if (path.BeforeSuffix("$", literal) && IsLiteralPath(literal)) {
            return new TExactMatcher(literal);
        }
        if (path.BeforeSuffix(".*", literal) && IsLiteralPath(literal)) {
            return new TPrefixMatcher(literal);
        }
        if (IsRegularPath(path)) {
            return new TPrefixMatcher(path);
        }
    }

    return new TRE2Matcher(regex);
}
