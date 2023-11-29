#include "swig_parser.h"

#include "cpp_includes_parser.h"

#include <library/cpp/regex/pcre/pcre.h>

#include <util/string/cast.h>
#include <util/string/strip.h>

namespace {
    const NPcre::TPcre INCLUDE_PATTERN = R"re(^%(include|import|insert\s*\([^\)]*\))\s*([<"].*?[">]))re";
}

void TSwigIncludesParser::Parse(IContentHolder& file, TVector<TInclDep>& includes) {
    TStringBuf line;
    bool inBlock = false;

    TStringBuf input = file.GetContent();
    TVector<TString> induced;

    while (input.ReadLine(line)) {
        line = StripString(line);

        if (!inBlock && (line.StartsWith("%include"sv) || line.StartsWith("%import"sv) || line.StartsWith("%insert"sv))) {
            if (auto matches = INCLUDE_PATTERN.Capture(line); matches.size() == 3 && matches[2].first >= 0) {
                includes.emplace_back(TString(line.data(), matches[2].first, matches[2].second - matches[2].first), false);
            }
        }
        if (line.StartsWith("%{"sv) || line.EndsWith("%{"sv)) {
            // Support "%{ (void)x;", "%header %{", "%insert(x) %{".
            inBlock = true;
        }
        if (line.EndsWith("%}"sv)) {
            // Support "%{ (void)x; %}".
            inBlock = false;
        }
        if (inBlock && line.StartsWith("#"sv)){
            ScanCppIncludes(line, induced);
        }
    }

    for (const TString& s : induced) {
        includes.emplace_back(s, true);
    }
}
