#include "xs_parser.h"

#include <library/cpp/regex/pcre/pcre.h>

#include <util/string/cast.h>
#include <util/string/strip.h>

namespace {
    const NPcre::TPcre INCLUDE_PATTERN = R"re(^INCLUDE\s*:\s*(\S+))re";
    const NPcre::TPcre INDUCED_PATTERN = R"re(^#include\s*["<]([^">]+)[>"])re";
}

void TXsIncludesParser::Parse(IContentHolder& file, TVector<TInclDep>& includes) const {
    TStringBuf line;
    TStringBuf input = file.GetContent();

    while (input.ReadLine(line)) {
        line = StripString(line);

        if (line.StartsWith("#include")) {
            if (auto matches = INDUCED_PATTERN.Capture(line); matches.size() == 2 && matches[1].first >= 0 && !line.Contains("// Y_IGNORE")) {
                includes.emplace_back(TString(line.data(), matches[1].first, matches[1].second - matches[1].first), true);
            }
        } else if (line.StartsWith("INCLUDE")) {
            if (auto matches = INCLUDE_PATTERN.Capture(line); matches.size() == 2 && matches[1].first >= 0) {
                includes.emplace_back(TString(line.data(), matches[1].first, matches[1].second - matches[1].first), false);
            }
        }
    }
}
