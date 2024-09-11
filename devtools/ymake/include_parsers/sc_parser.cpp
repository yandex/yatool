#include "sc_parser.h"

#include <library/cpp/regex/pcre/pcre.h>

#include <util/string/cast.h>
#include <util/string/strip.h>
#include <util/string/subst.h>

namespace {
    const NPcre::TPcre INCLUDE_PATTERN = R"re(^include\s+"([^"]+)"\s*;)re";
}

void TScIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) const {
    TStringBuf line;
    TStringBuf input = file.GetContent();

    while (input.ReadLine(line)) {
        line = StripString(line);

        if (line.StartsWith("include"sv)) {
            if (auto matches = INCLUDE_PATTERN.Capture(line); matches.size() == 2 && matches[1].first >= 0) {
                includes.emplace_back(line.data(), matches[1].first, matches[1].second - matches[1].first);
            }
        }
    }
}
