#include "fortran_parser.h"

#include <library/cpp/regex/pcre/pcre.h>

#include <util/string/cast.h>
#include <util/string/strip.h>

namespace {
    const NPcre::TPcre INCLUDE_PATTERN = R"re(^include\s+'([^']+)')re";
}

void TFortranIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) {
    TStringBuf line;
    TStringBuf input = file.GetContent();

    while (input.ReadLine(line)) {
        line = StripString(line);
        if (!line.empty() && (line[0] == '!' || line[0] == 'c')) {
            continue;
        }

        if (line.StartsWith("include"sv)) {
            if (auto matches = INCLUDE_PATTERN.Capture(line); matches.size() == 2 && matches[1].first >= 0) {
                includes.emplace_back(line.data(), matches[1].first, matches[1].second - matches[1].first);
            }
        }
    }
}
