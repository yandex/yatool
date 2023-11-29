#include "cfgproto_parser.h"

#include <library/cpp/regex/pcre/pcre.h>

#include <util/string/cast.h>
#include <util/string/strip.h>

namespace {
    const NPcre::TPcre IMPORT_PATTERN = R"re(^import\s+"([^"]+)")re";
    const NPcre::TPcre INDUCED_PATTERN = R"re(^option\s+\(\s*NProtoConfig\.Include\s*\)\s*=\s*"([^"]+)")re";
}

void TCfgprotoIncludesParser::Parse(IContentHolder& file, TVector<TInclDep>& includes) {
    TStringBuf line;
    TStringBuf input = file.GetContent();

    while (input.ReadLine(line)) {
        line = StripString(line);

        if (line.StartsWith("option"sv)) {
            if (auto matches = INDUCED_PATTERN.Capture(line); matches.size() == 2 && matches[1].first >= 0 && !line.Contains("// Y_IGNORE"sv)) {
                includes.emplace_back(TString(line.data(), matches[1].first, matches[1].second - matches[1].first), true);
            }
        } else if (line.StartsWith("import"sv)) {
            if (auto matches = IMPORT_PATTERN.Capture(line); matches.size() == 2 && matches[1].first >= 0) {
                includes.emplace_back(TString(line.data(), matches[1].first, matches[1].second - matches[1].first), false);
            }
        }
    }
}

