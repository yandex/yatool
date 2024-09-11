#include "mapkit_idl_parser.h"

#include <devtools/ymake/common/content_holder.h>

#include <library/cpp/regex/pcre/pcre.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/strip.h>
#include <util/system/yassert.h>

namespace {
    const NPcre::TPcre IMPORT_PATTERN = R"re(^import\s+"([^"]+)")re";
    const NPcre::TPcre BASED_ON_PATTERN = R"re(based\s+on\s+"([^"]+)"\s*:)re";
    const NPcre::TPcre Y_IGNORE_PATTERN = R"re(//\s*Y_IGNORE\s*$$)re";
}

void TMapkitIdlIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) const {
    TStringBuf line;
    NPcre::TPcreMatches result;

    TStringBuf input = file.GetContent();
    while (input.ReadLine(line)) {
        result.clear();
        line = StripStringLeft(line);

        if (line.StartsWith("import"sv)) {
            result = IMPORT_PATTERN.Capture(line);
        } else if (line.size() > 12) {
            result = BASED_ON_PATTERN.Capture(line);
        }

        if (result.size() == 2 && result[1].first >= 0) {
            if (!Y_IGNORE_PATTERN.Matches(line)) {
                includes.push_back(TString(line.data(), result[1].first, result[1].second - result[1].first));
            }
        }
    }
}
