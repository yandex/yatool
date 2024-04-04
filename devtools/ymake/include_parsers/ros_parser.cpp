#include "ros_parser.h"

#include <library/cpp/regex/pcre/pcre.h>

#include <util/generic/set.h>
#include <util/string/strip.h>

namespace {

    const NPcre::TPcre ENUM_PATTERN = R"re(^enum\s+(\w+)\s*(\w+)?$)re";
    const NPcre::TPcre FIELD_PATTERN = R"re(^(?:Optional<([\w/]+)(?:\[\d*\])?>|([\w/]+))(?:\[\d*\])?\s+\w+$)re";

    TStringBuf StripLine(TStringBuf rawLine) {
        TStringBuf line, comment;
        rawLine.Split('#', line, comment);
        return StripString(line);
    }

    const TSet<TStringBuf> BUILTIN_TYPES = {
        "int8",
        "int16",
        "int32",
        "int64",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
        "float32",
        "float64",
        "bool",
        "string",
        "time",
        "duration",
        "byte",
        "char",
    };

} // namespace

void TRosIncludeParser::Parse(IContentHolder& file, TVector<TRosDep>& parsedIncludes) {
    TStringBuf input = file.GetContent();
    TStringBuf line;

    TSet<TString> enumNames;

    while (input.ReadLine(line)) {
        line = StripLine(line);

        if (auto matches = ENUM_PATTERN.Capture(line); matches.size() >= 2) {
            TStringBuf enumName = line.SubString(matches[1].first, matches[1].second - matches[1].first);

            enumNames.emplace(enumName);
        } else if (auto matches = FIELD_PATTERN.Capture(line); matches.size() >= 2) {
            TStringBuf typeName;

            if (matches[1].first >= 0) {
                typeName = line.SubString(matches[1].first, matches[1].second - matches[1].first);
            } else if (matches[2].first >= 0) {
                typeName = line.SubString(matches[2].first, matches[2].second - matches[2].first);
            } else {
                continue;
            }

            if (BUILTIN_TYPES.contains(typeName)) {
                continue;
            }

            if (enumNames.contains(typeName)) {
                continue;
            }

            TStringBuf packageName, messageName;

            if (!typeName.TrySplit('/', packageName, messageName)) {
                if (typeName == "Header") {
                    packageName = "std_msgs";
                }

                messageName = typeName;
            }

            parsedIncludes.emplace_back(packageName, messageName);
        }
    }
}
